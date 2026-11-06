#include "pc/ClipboardHistory.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

namespace ravenspot {

ClipboardHistory::ClipboardHistory(QObject *parent)
    : QObject(parent)
{
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this]() { connectTo(m_socketPath); });
}

QString ClipboardHistory::defaultSocketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/orkun-clipboard.sock");
}

std::optional<ClipEntry> ClipboardHistory::parseEvent(const QByteArray &line) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    const QString text = o.value(QStringLiteral("text")).toString();
    if (text.trimmed().isEmpty()) return std::nullopt; // yalnızca metin (görsel/html yok)
    ClipEntry e;
    e.text = text;
    e.fingerprint = o.value(QStringLiteral("fingerprint")).toString();
    if (e.fingerprint.isEmpty())
        e.fingerprint = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
    e.timestamp = static_cast<qint64>(o.value(QStringLiteral("timestamp")).toDouble());
    return e;
}

void ClipboardHistory::ingest(const ClipEntry &entry) {
    ClipEntry e = entry;
    if (e.text.size() > kMaxChars) {
        qsizetype cut = kMaxChars;
        if (e.text.at(cut - 1).isHighSurrogate()) --cut; // vekil çifti bölme (emoji vb.)
        e.text.truncate(cut);
        e.truncated = true;
    }
    // Aynı içerik tekrar kopyalanırsa başa taşı (çoğaltma)
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&](const ClipEntry &x) { return x.fingerprint == e.fingerprint; }),
                    m_entries.end());
    m_entries.prepend(e);
    while (m_entries.size() > kMax) m_entries.removeLast();
    emit changed();
}

QList<ClipEntry> ClipboardHistory::matching(const QString &query, int limit) const {
    QList<ClipEntry> out;
    const QString q = query.trimmed();
    for (const ClipEntry &e : m_entries) {
        if (out.size() >= limit) break;
        if (q.isEmpty() || e.text.contains(q, Qt::CaseInsensitive)) out << e;
    }
    return out;
}

void ClipboardHistory::connectTo(const QString &socketPath) {
    m_socketPath = socketPath;
    if (m_socket) { m_socket->disconnect(this); m_socket->abort(); m_socket->deleteLater(); }
    m_buffer.clear();
    m_discarding = false;

    auto *s = new QLocalSocket(this);
    m_socket = s;
    connect(s, &QLocalSocket::connected, this, [this]() { m_backoffMs = 1000; });
    connect(s, &QLocalSocket::readyRead, this, &ClipboardHistory::onReadyRead);
    connect(s, &QLocalSocket::disconnected, this, &ClipboardHistory::scheduleReconnect);
    connect(s, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { scheduleReconnect(); });
    s->connectToServer(socketPath, QIODevice::ReadOnly);
}

void ClipboardHistory::scheduleReconnect() {
    if (m_reconnect.isActive() || m_socketPath.isEmpty()) return;
    // Daemon yeniden başlarsa geri bağlan: 1 s -> 30 s artan bekleme
    m_reconnect.start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, 30000);
}

void ClipboardHistory::onReadyRead() {
    if (!m_socket) return;
    m_buffer += m_socket->readAll();
    while (true) {
        const qsizetype nl = m_buffer.indexOf('\n');
        if (nl < 0) {
            if (m_buffer.size() > kMaxLineBytes) { m_buffer.clear(); m_discarding = true; } // aşırı uzun satır
            return;
        }
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (m_discarding) { m_discarding = false; continue; }
        if (const auto e = parseEvent(line)) ingest(*e);
    }
}

} // namespace ravenspot
