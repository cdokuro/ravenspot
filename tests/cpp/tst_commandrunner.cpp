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
    void stop_killsWholeProcessGroup() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("sleep 731.5 & sleep 732.5 | cat; wait"));
        QTest::qWait(300);
        r.stop();
        QVERIFY(done.wait(3500));
        QTest::qWait(200);
        QProcess pg;
        pg.start("pgrep", {"-f", "sleep 73[12].5"});
        pg.waitForFinished(2000);
        QVERIFY2(pg.readAllStandardOutput().trimmed().isEmpty(), "orphaned children left running");
    }
    void output_longLineCapped() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("head -c 200000 /dev/zero | tr '\\0' x"));
        QVERIFY(done.wait(5000));
        QVERIFY2(r.output().size() <= CommandRunner::kMaxLineChars + 1, qPrintable(QString::number(r.output().size())));
    }
    void output_carriageReturnSplitsLines() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("printf 'a\\rb\\n'"));
        QVERIFY(done.wait(3000));
        QCOMPARE(r.output(), QStringLiteral("a\nb"));
    }
    void stdin_isClosed() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("read x; echo got"));
        QVERIFY2(done.wait(2000), "command reading stdin must not hang");
        if (r.isRunning()) r.stop();
    }
    void utf8_splitAcrossChunks() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("printf '\\304'; sleep 0.3; printf '\\237\\n'"));
        QVERIFY(done.wait(3000));
        QCOMPARE(r.output(), QString::fromUtf8("\xc4\x9f"));
    }

};

QObject *makeTestCommandRunner() { return new TestCommandRunner; }
#include "tst_commandrunner.moc"
