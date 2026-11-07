#include "run/CommandRunner.hpp"
#include "run/CommandAnalyzer.hpp"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#endif

namespace ravenspot {

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
    m_emitTimer.setSingleShot(true);
    m_emitTimer.setInterval(50); // yüksek hacimli çıktıda QML'i boğmamak için toplu bildir
    connect(&m_emitTimer, &QTimer::timeout, this, &CommandRunner::outputChanged);
}

namespace {
#ifdef Q_OS_WIN
// Windows has no process groups/SIGTERM for console programs: the command's
// process tree lives in a Job object and stop() terminates the job.
#ifndef SIGTERM
#define SIGTERM 15
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
void signalGroup(qint64, int) {}
void closeJob(void *&job, bool terminate) {
    if (!job) return;
    if (terminate) ::TerminateJobObject(static_cast<HANDLE>(job), 1);
    ::CloseHandle(static_cast<HANDLE>(job));
    job = nullptr;
}
#else
// Komut kendi oturumunda/grubunda başladığı için sinyal tüm gruba gider:
// arka plana atılan işler ve pipeline'lar da durur (yalnızca bash değil).
void signalGroup(qint64 pgid, int sig) {
    if (pgid > 1) ::kill(static_cast<pid_t>(-pgid), sig);
}
#endif
} // namespace

CommandRunner::~CommandRunner() {
    if (m_proc) {
        m_proc->disconnect(this);
        signalGroup(m_pgid, SIGKILL);
#ifdef Q_OS_WIN
        closeJob(m_job, true);
#endif
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
#ifdef Q_OS_WIN
    closeJob(m_job, false);
#endif
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
#ifdef Q_OS_WIN
    // Pasted commands on Windows are PowerShell; -Command takes the text as one argv item.
    return launch(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-Command"),
                   // pipe output as UTF-8 (the decoder below expects it; default is the OEM code page)
                   QStringLiteral("[Console]::OutputEncoding=[Text.Encoding]::UTF8; ") + command},
                  command);
#else
    return launch(QStringLiteral("bash"), cmd::inlineArgs(command), command);
#endif
}

bool CommandRunner::startProgram(const QString &program, const QStringList &args, const QString &displayCommand) {
    if (m_proc || program.isEmpty()) return false;
    return launch(program, args, displayCommand);
}

bool CommandRunner::launch(const QString &program, const QStringList &args, const QString &displayCommand) {
    m_command = displayCommand;
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
#ifdef Q_OS_WIN
    closeJob(m_job, false);
    m_job = ::CreateJobObjectW(nullptr, nullptr);
    if (m_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        // RavenSpot quitting also ends the command (same as the Linux destructor)
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(static_cast<HANDLE>(m_job), JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    // No console window for the inline runner
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
    });
#else
    proc->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif

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
    connect(proc, &QProcess::errorOccurred, this, [this, proc, program](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return; // diğerlerinde finished da gelir
        m_lines << QStringLiteral("[failed to start %1]").arg(QFileInfo(program).fileName());
        m_exitCode = 127;
        m_proc = nullptr;
        proc->deleteLater();
        emit outputChanged();
        emit runningChanged();
        emit finished(m_exitCode, true);
    });

    connect(proc, &QProcess::started, this, [this, proc]() {
        m_pgid = proc->processId();
#ifdef Q_OS_WIN
        // Children spawned after this point inherit the job. (A child created in
        // the first instant before assignment could escape; acceptable here.)
        if (m_job && m_pgid > 0) {
            if (HANDLE h = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(m_pgid))) {
                ::AssignProcessToJobObject(static_cast<HANDLE>(m_job), h);
                ::CloseHandle(h);
            }
        }
#endif
    });
    proc->start(program, args);
    emit runningChanged();
    emit outputChanged();
    return true;
}

void CommandRunner::stop() {
    if (!m_proc) return;
    const qint64 pgid = m_pgid;
    QPointer<QProcess> proc = m_proc;
    signalGroup(pgid, SIGTERM);
#ifdef Q_OS_WIN
    // terminate() only posts WM_CLOSE, which console programs ignore: end the tree now.
    closeJob(m_job, true);
    if (proc) proc->kill();
    return;
#endif
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
