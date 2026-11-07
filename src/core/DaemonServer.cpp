#include "DaemonServer.hpp"
#include "SingleInstance.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace ravenspot {

DaemonServer::DaemonServer(const QString &socketPath, QObject *parent)
    : DaemonServer(socketPath, DEFAULT_TIMEOUT_MS, parent)
{
}

DaemonServer::DaemonServer(const QString &socketPath, int timeoutMs, QObject *parent)
    : QObject(parent)
    , m_socketPath(socketPath)
    , m_timeoutMs(timeoutMs > 0 ? timeoutMs : DEFAULT_TIMEOUT_MS)
{
    connect(&m_server, &QLocalServer::newConnection, this, &DaemonServer::onNewConnection);
}

DaemonServer::~DaemonServer() {
    stop();
}

bool DaemonServer::start(const QString &socketPath) {
    if (!socketPath.isEmpty()) {
        m_socketPath = socketPath;
    }
    if (m_socketPath.isEmpty()) {
        m_socketPath = QString::fromStdString(SingleInstance::socketPath());
    }

    // Clean up any stale registration in Qt
    QLocalServer::removeServer(m_socketPath);

    // Enforce 0600 mode permissions on Unix domain socket
    m_server.setSocketOptions(QLocalServer::UserAccessOption);

    m_startTimeSecs = QDateTime::currentSecsSinceEpoch();

    bool ok = m_server.listen(m_socketPath);
    if (!ok) {
        qCritical() << "DaemonServer: Failed to listen on socket" << m_socketPath 
                    << ":" << m_server.errorString();
    } else {
        qInfo() << "DaemonServer: Listening on socket" << m_socketPath 
                << "(client timeout:" << m_timeoutMs << "ms)";
    }
    return ok;
}

void DaemonServer::stop() {
    if (m_server.isListening()) {
        m_server.close();
        QLocalServer::removeServer(m_socketPath);
#ifndef Q_OS_WIN
        ::unlink(m_socketPath.toUtf8().constData());
#endif
    }

    // Disconnect and cleanly abort all active client connections
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QLocalSocket *client = it.key();
        if (it->timer) {
            it->timer->stop();
        }
        if (client) {
            client->disconnect(); // Prevent callbacks during stop
            client->abort();
            client->deleteLater();
        }
    }
    m_clients.clear();
}

bool DaemonServer::isListening() const {
    return m_server.isListening();
}

QString DaemonServer::socketPath() const {
    return m_socketPath;
}

int DaemonServer::timeoutMs() const {
    return m_timeoutMs;
}

void DaemonServer::setTimeoutMs(int timeoutMs) {
    m_timeoutMs = (timeoutMs > 0) ? timeoutMs : DEFAULT_TIMEOUT_MS;
}

void DaemonServer::setStatusProvider(StatusProvider provider) {
    m_statusProvider = std::move(provider);
}

void DaemonServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket *client = m_server.nextPendingConnection();
        if (!client) continue;

        auto *timer = new QTimer(client);
        timer->setSingleShot(true);
        timer->setInterval(m_timeoutMs);

        connect(timer, &QTimer::timeout, this, [this, client]() {
            onClientTimeout(client);
        });

        m_clients.insert(client, ClientSession{QByteArray(), timer});

        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            onClientReadyRead(client);
        });

        connect(client, &QLocalSocket::disconnected, this, [this, client]() {
            onClientDisconnected(client);
        });

        // Start inactivity timer upon connection acceptance
        timer->start();

        if (client->bytesAvailable() > 0) {
            onClientReadyRead(client);
        }
    }
}

void DaemonServer::onClientReadyRead(QLocalSocket *client) {
    if (!client) return;
    auto it = m_clients.find(client);
    if (it == m_clients.end()) return;

    it->buffer.append(client->readAll());
    QByteArray &buffer = it->buffer;

    // Protect against unbounded buffer growth
    if (buffer.size() > 131072 && buffer.indexOf('\n') == -1) {
        buffer.clear();
        if (it->timer) {
            it->timer->stop();
        }
        sendResponse(client, "error: command payload too large\n");
        return;
    }

    int newlineIndex;
    bool dispatchedAny = false;
    while ((newlineIndex = buffer.indexOf('\n')) != -1) {
        dispatchedAny = true;
        QByteArray lineBytes = buffer.left(newlineIndex).trimmed();
        buffer.remove(0, newlineIndex + 1);

        if (lineBytes.isEmpty()) {
            sendResponse(client, "error: empty command\n");
        } else {
            QString cmd = QString::fromUtf8(lineBytes).toLower();
            dispatchCommand(client, cmd);
        }
    }

    // Refresh or stop timer based on stream state
    if (it->timer) {
        if (dispatchedAny && buffer.isEmpty()) {
            // All commands handled, response sent; stop inactivity timer
            it->timer->stop();
        } else {
            // Partial command or trailing bytes; refresh timer
            it->timer->start();
        }
    }
}

void DaemonServer::onClientDisconnected(QLocalSocket *client) {
    if (client) {
        auto it = m_clients.find(client);
        if (it != m_clients.end()) {
            if (it->timer) {
                it->timer->stop();
            }
            m_clients.erase(it);
        }
        client->deleteLater();
    }
}

void DaemonServer::onClientTimeout(QLocalSocket *client) {
    if (!client) return;

    auto it = m_clients.find(client);
    if (it == m_clients.end()) return;

    qWarning() << "DaemonServer: Client connection timed out waiting for command ("
               << m_timeoutMs << "ms inactivity timeout); terminating connection.";

    if (it->timer) {
        it->timer->stop();
    }
    it->buffer.clear();

    if (client->state() == QLocalSocket::ConnectedState) {
        client->write("error: connection timed out\n");
        client->flush();
        client->disconnectFromServer();
    } else {
        client->abort();
    }

    // Safety fallback: if graceful disconnect does not transition to UnconnectedState within 100ms, force abort
    QTimer::singleShot(100, client, [client]() {
        if (client && client->state() != QLocalSocket::UnconnectedState) {
            client->abort();
        }
    });
}

void DaemonServer::dispatchCommand(QLocalSocket *client, const QString &cmd) {
    qInfo() << "DaemonServer: Command received ->" << cmd;

    if (cmd == QStringLiteral("toggle") || cmd == QStringLiteral("reopen")) {
        emit toggleRequested();
        sendResponse(client, "ok\n");
    } else if (cmd == QStringLiteral("show")) {
        emit showRequested();
        sendResponse(client, "ok\n");
    } else if (cmd == QStringLiteral("hide")) {
        emit hideRequested();
        sendResponse(client, "ok\n");
    } else if (cmd == QStringLiteral("reload")) {
        emit reloadRequested();
        sendResponse(client, "ok\n");
    } else if (cmd == QStringLiteral("quit") || cmd == QStringLiteral("exit")) {
        emit quitRequested();
        sendResponse(client, "ok\n");
    } else if (cmd == QStringLiteral("status")) {
        emit statusRequested();
        QJsonObject root;
        if (m_statusProvider) {
            root = m_statusProvider();
        }
        if (!root.contains(QStringLiteral("status"))) {
            root[QStringLiteral("status")] = QStringLiteral("ok");
        }
        if (!root.contains(QStringLiteral("visible"))) {
            root[QStringLiteral("visible")] = false;
        }
        if (!root.contains(QStringLiteral("uptime"))) {
            qint64 now = QDateTime::currentSecsSinceEpoch();
            root[QStringLiteral("uptime")] = static_cast<qint64>(now - m_startTimeSecs);
        }
        if (!root.contains(QStringLiteral("theme"))) {
            root[QStringLiteral("theme")] = QStringLiteral("raven_cyber");
        }
#ifdef Q_OS_WIN
        root[QStringLiteral("pid")] = QCoreApplication::applicationPid();
#else
        root[QStringLiteral("pid")] = static_cast<qint64>(::getpid());
#endif

        QJsonDocument doc(root);
        sendResponse(client, doc.toJson(QJsonDocument::Compact) + "\n");
    } else {
        QByteArray err = QString("error: unknown command '%1'\n").arg(cmd).toUtf8();
        sendResponse(client, err);
    }
}

void DaemonServer::sendResponse(QLocalSocket *client, const QByteArray &data) {
    if (!client || client->state() != QLocalSocket::ConnectedState) return;

    auto it = m_clients.find(client);
    if (it != m_clients.end() && it->timer) {
        it->timer->stop();
    }

    client->write(data);
    client->flush();
    // Allow response to be fully transmitted before disconnecting client
    QTimer::singleShot(25, client, &QLocalSocket::disconnectFromServer);
}

} // namespace ravenspot
