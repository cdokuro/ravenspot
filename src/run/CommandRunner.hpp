#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QStringDecoder>

namespace ravenspot {

// Tek bir satır içi (inline) komutu arka planda çalıştırır; çıktıyı (stdout+stderr)
// en fazla kMaxLines satır tutar ve QML'e 50 ms'de bir toplu bildirir.
// Boştayken QProcess yoktur (bellek maliyeti ~0).
class CommandRunner : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString command READ command NOTIFY runningChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY finished)

public:
    static constexpr int kMaxLines = 2000;
    static constexpr int kKillGraceMs = 2000;
    static constexpr int kMaxLineChars = 4000; // tek satır sınırı (küçültülmüş JSON, ikili çıktı)

    explicit CommandRunner(QObject *parent = nullptr);
    ~CommandRunner() override;

    [[nodiscard]] bool isRunning() const { return m_proc != nullptr; }
    [[nodiscard]] QString output() const;
    [[nodiscard]] QString command() const { return m_command; }
    [[nodiscard]] int exitCode() const { return m_exitCode; }

    Q_INVOKABLE bool start(const QString &command);
    // Runs a program with an argv list (no shell). displayCommand is what the
    // output panel and the finish notification show.
    bool startProgram(const QString &program, const QStringList &args, const QString &displayCommand);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();

signals:
    void outputChanged();
    void runningChanged();
    void finished(int exitCode, bool crashed);

private:
    bool launch(const QString &program, const QStringList &args, const QString &displayCommand);
    void appendChunk(const QByteArray &data);
    void scheduleOutputSignal();

    QPointer<QProcess> m_proc;
    QString m_command;
    QStringList m_lines;
    QString m_partial; // satır sonu gelmemiş son parça
    bool m_partialTruncated{false}; // kMaxLineChars aşıldı: satır sonuna kadar at
    QStringDecoder m_decoder{QStringDecoder::Utf8}; // chunk sınırında bölünen UTF-8'i birleştirir
    qint64 m_pgid{0}; // komutun süreç grubu (kendi oturumunda başlar)
#ifdef Q_OS_WIN
    void *m_job{nullptr}; // Job object: stop() ends the whole process tree
#endif
    int m_exitCode{0};
    QTimer m_emitTimer;
};

} // namespace ravenspot
