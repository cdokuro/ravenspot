#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonObject>
#include <QHash>
#include <functional>

class QTimer;

namespace ravenspot {

class DaemonServer : public QObject {
    Q_OBJECT

public:
    using StatusProvider = std::function<QJsonObject()>;
    static constexpr int DEFAULT_TIMEOUT_MS = 2000;

    explicit DaemonServer(const QString &socketPath = QString(), QObject *parent = nullptr);
    DaemonServer(const QString &socketPath, int timeoutMs, QObject *parent = nullptr);
    ~DaemonServer() override;

    /**
     * @brief Starts the Unix socket listener on the given path.
     * @param socketPath Path to bind (defaults to SingleInstance::socketPath())
     * @return true if listener bound successfully; false otherwise.
     */
    bool start(const QString &socketPath = QString());

    /**
     * @brief Stops listener, disconnects clients, and removes socket.
     */
    void stop();

    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString socketPath() const;
    [[nodiscard]] int timeoutMs() const;
    void setTimeoutMs(int timeoutMs);

    /**
     * @brief Registers a callback to provide telemetry/theme data for 'status' command.
     */
    void setStatusProvider(StatusProvider provider);

signals:
    void toggleRequested();
    void showRequested();
    void hideRequested();
    void reloadRequested();
    void quitRequested();
    void statusRequested();

private slots:
    void onNewConnection();
    void onClientReadyRead(QLocalSocket *client);
    void onClientDisconnected(QLocalSocket *client);
    void onClientTimeout(QLocalSocket *client);

private:
    struct ClientSession {
        QByteArray buffer;
        QTimer *timer{nullptr};
    };

    void dispatchCommand(QLocalSocket *client, const QString &cmd);
    void sendResponse(QLocalSocket *client, const QByteArray &data);

    QLocalServer m_server;
    QString m_socketPath;
    StatusProvider m_statusProvider;
    QHash<QLocalSocket*, ClientSession> m_clients;
    qint64 m_startTimeSecs{0};
    int m_timeoutMs{DEFAULT_TIMEOUT_MS};
};

} // namespace ravenspot
