#include "run/CommandRunner.hpp"
#include "run/CommandAnalyzer.hpp"

#include <QDir>

namespace ravenspot {

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
    m_emitTimer.setSingleShot(true);
    m_emitTimer.setInterval(50); // yüksek hacimli çıktıda QML'i boğmamak için toplu bildir
    connect(&m_emitTimer, &QTimer::timeout, this, &CommandRunner::outputChanged);
}

CommandRunner::~CommandRunner() {
    if (m_proc) {
        m_proc->disconnect(this);
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
    m_exitCode = 0;

    auto *proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setWorkingDirectory(QDir::homePath());

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

    proc->start(QStringLiteral("bash"), cmd::inlineArgs(command));
    emit runningChanged();
    emit outputChanged();
    return true;
}

void CommandRunner::stop() {
    if (!m_proc) return;
    QPointer<QProcess> proc = m_proc;
    proc->terminate();
    // SIGTERM'i yok sayan süreçler için zorla sonlandır.
    QTimer::singleShot(kKillGraceMs, this, [proc]() {
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
    const QString text = m_partial + QString::fromUtf8(data);
    QStringList parts = text.split(QLatin1Char('\n'));
    m_partial = parts.takeLast(); // son parça satır sonu bekliyor
    m_lines.append(parts);
    while (m_lines.size() > kMaxLines) m_lines.removeFirst();
    scheduleOutputSignal();
}

void CommandRunner::scheduleOutputSignal() {
    if (!m_emitTimer.isActive()) m_emitTimer.start();
}

} // namespace ravenspot
