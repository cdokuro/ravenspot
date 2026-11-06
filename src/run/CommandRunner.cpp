#include "run/CommandRunner.hpp"
#include "run/CommandAnalyzer.hpp"

#include <QDir>

#include <csignal>
#include <sys/types.h>

namespace ravenspot {

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
    m_emitTimer.setSingleShot(true);
    m_emitTimer.setInterval(50); // yüksek hacimli çıktıda QML'i boğmamak için toplu bildir
    connect(&m_emitTimer, &QTimer::timeout, this, &CommandRunner::outputChanged);
}

namespace {
// Komut kendi oturumunda/grubunda başladığı için sinyal tüm gruba gider:
// arka plana atılan işler ve pipeline'lar da durur (yalnızca bash değil).
void signalGroup(qint64 pgid, int sig) {
    if (pgid > 1) ::kill(static_cast<pid_t>(-pgid), sig);
}
} // namespace

CommandRunner::~CommandRunner() {
    if (m_proc) {
        m_proc->disconnect(this);
        signalGroup(m_pgid, SIGKILL);
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

QString CommandRunner::output() const {
    QString out = m_lines.join(QLatin1Char('\n'));
    if (!m_partial.isEmpty()) {
        if (!out.isEmpty()) out += QLatin1Char('\n');
        out += m_partial;
    }
    return out;
}

bool CommandRunner::start(const QString &command) {
    if (m_proc || command.trimmed().isEmpty()) return false;

    m_command = command;
    m_lines.clear();
    m_partial.clear();
    m_partialTruncated = false;
    m_decoder.resetState();
    m_exitCode = 0;
    m_pgid = 0;

    auto *proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setWorkingDirectory(QDir::homePath());
    // Girdi beklemesin: "[Y/n]" soran bir komut sonsuza dek "Running…" kalmasın.
    proc->setStandardInputFile(QProcess::nullDevice());
    proc->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);

    connect(proc, &QProcess::readyRead, this, [this, proc]() { appendChunk(proc->readAll()); });
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        appendChunk(proc->readAll());
        if (!m_partial.isEmpty()) { m_lines << m_partial; m_partial.clear(); }
        while (m_lines.size() > kMaxLines) m_lines.removeFirst();
        const bool crashed = (status == QProcess::CrashExit);
        m_exitCode = crashed ? -1 : code;
        m_proc = nullptr;
        proc->deleteLater();
        m_emitTimer.stop();
        emit outputChanged();
        emit runningChanged();
        emit finished(m_exitCode, crashed);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return; // diğerlerinde finished da gelir
        m_lines << QStringLiteral("[failed to start bash]");
        m_exitCode = 127;
        m_proc = nullptr;
        proc->deleteLater();
        emit outputChanged();
        emit runningChanged();
        emit finished(m_exitCode, true);
    });

    connect(proc, &QProcess::started, this, [this, proc]() { m_pgid = proc->processId(); });
    proc->start(QStringLiteral("bash"), cmd::inlineArgs(command));
    emit runningChanged();
    emit outputChanged();
    return true;
}

void CommandRunner::stop() {
    if (!m_proc) return;
    const qint64 pgid = m_pgid;
    QPointer<QProcess> proc = m_proc;
    signalGroup(pgid, SIGTERM);
    proc->terminate();
    // SIGTERM'i yok sayanlar için (bash bitmiş olsa bile gruptaki çocuklar) zorla sonlandır.
    QTimer::singleShot(kKillGraceMs, this, [proc, pgid]() {
        signalGroup(pgid, SIGKILL);
        if (proc && proc->state() != QProcess::NotRunning) proc->kill();
    });
}

void CommandRunner::clear() {
    if (m_proc) return;
    m_lines.clear();
    m_partial.clear();
    m_command.clear();
    m_exitCode = 0;
    emit outputChanged();
    emit runningChanged();
}

void CommandRunner::appendChunk(const QByteArray &data) {
    if (data.isEmpty()) return;
    QString text = m_decoder.decode(data);
    // \r (ilerleme çubukları) ve \r\n satır sonu sayılır
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList parts = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const bool last = (i == parts.size() - 1);
        if (!m_partialTruncated) {
            m_partial += parts.at(i);
            if (m_partial.size() > kMaxLineChars) {
                // Tek satır sınırı: belleği ve TextEdit'i korumak için kes, satır sonuna dek at
                m_partial.truncate(kMaxLineChars);
                m_partial += QChar(0x2026);
                m_partialTruncated = true;
            }
        }
        if (!last) {
            m_lines << m_partial;
            m_partial.clear();
            m_partialTruncated = false;
        }
    }
    while (m_lines.size() > kMaxLines) m_lines.removeFirst();
    scheduleOutputSignal();
}

void CommandRunner::scheduleOutputSignal() {
    if (!m_emitTimer.isActive()) m_emitTimer.start();
}

} // namespace ravenspot
