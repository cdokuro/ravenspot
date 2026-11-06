#pragma once

#include <QByteArray>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <optional>

namespace ravenspot {

struct ClipEntry {
    QString text;
    QString fingerprint;
    qint64 timestamp{0};
};

// orkun-clipboard-daemon'un NDJSON olaylarına abone olur ve son metinleri
// YALNIZCA bellekte tutar (diske yazılmaz). Daemon gizli öğeleri zaten ayıklar.
class ClipboardHistory : public QObject {
    Q_OBJECT
public:
    static constexpr int kMax = 50;
    static constexpr int kMaxChars = 4096;
    static constexpr int kMaxLineBytes = 1024 * 1024;

    explicit ClipboardHistory(QObject *parent = nullptr);

    static std::optional<ClipEntry> parseEvent(const QByteArray &line);
    static QString defaultSocketPath();

    void ingest(const ClipEntry &entry);
    [[nodiscard]] QList<ClipEntry> entries() const { return m_entries; }
    [[nodiscard]] QList<ClipEntry> matching(const QString &query, int limit) const;

    void connectTo(const QString &socketPath);

signals:
    void changed();

private:
    void onReadyRead();
    void scheduleReconnect();

    QList<ClipEntry> m_entries;
    QString m_socketPath;
    QPointer<QLocalSocket> m_socket;
    QByteArray m_buffer;
    bool m_discarding{false};
    int m_backoffMs{1000};
    QTimer m_reconnect;
};

} // namespace ravenspot
