#include <QtTest>
#include "run/CommandRunner.hpp"

using ravenspot::CommandRunner;

class TestCommandRunner : public QObject {
    Q_OBJECT
private slots:
    void start_capturesOutputAndExit() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("echo hi; echo err >&2; exit 3"));
        QVERIFY(done.wait(3000));
        QCOMPARE(r.exitCode(), 3);
        QVERIFY(r.output().contains("hi"));
        QVERIFY(r.output().contains("err"));
        QVERIFY(!r.isRunning());
    }
    void start_refusedWhileRunning() {
        CommandRunner r;
        QVERIFY(r.start("sleep 5"));
        QVERIFY(!r.start("echo x"));
        QSignalSpy done(&r, &CommandRunner::finished);
        r.stop();
        QVERIFY(done.wait(3500));
    }
    void stop_escalatesToKill() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("trap '' TERM; sleep 30"));
        QTest::qWait(200);
        r.stop();
        QVERIFY(done.wait(3500));
    }
    void output_cappedAtMaxLines() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("seq 1 2500"));
        QVERIFY(done.wait(5000));
        const QStringList lines = r.output().split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), CommandRunner::kMaxLines);
        QCOMPARE(lines.first(), QStringLiteral("501"));
    }
    void start_specialCharsUnexpanded() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("printf '%s' '$HOME'"));
        QVERIFY(done.wait(3000));
        QCOMPARE(r.output(), QStringLiteral("$HOME"));
    }
};

QObject *makeTestCommandRunner() { return new TestCommandRunner; }
#include "tst_commandrunner.moc"
