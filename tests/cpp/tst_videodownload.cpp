#include <QtTest>
#include "net/VideoDownload.hpp"
#include "run/CommandRunner.hpp"

using namespace ravenspot;

// Pasted-link download: URL validation, yt-dlp argv and the argv runner it uses.
class TestVideoDownload : public QObject {
    Q_OBJECT
private slots:
    void isHttpUrl_acceptsWebLinks() {
        QVERIFY(video::isHttpUrl("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
        QVERIFY(video::isHttpUrl("http://example.com"));
        QVERIFY(video::isHttpUrl("  https://youtu.be/abc  "));
    }
    void isHttpUrl_rejectsEverythingElse() {
        QVERIFY(!video::isHttpUrl(""));
        QVERIFY(!video::isHttpUrl("youtube.com/watch?v=x"));      // no scheme
        QVERIFY(!video::isHttpUrl("file:///etc/passwd"));
        QVERIFY(!video::isHttpUrl("javascript:alert(1)"));
        QVERIFY(!video::isHttpUrl("ftp://example.com/x"));
        QVERIFY(!video::isHttpUrl("https://"));                   // no host
        QVERIFY(!video::isHttpUrl("https://a.com/x y"));          // spaces
        QVERIFY(!video::isHttpUrl("https://a.com\nrm -rf ~"));    // multi-line paste
    }
    void downloadArgs_urlLastAfterDoubleDash() {
        const QStringList a = video::downloadArgs(" https://x.com/v ", "/tmp/dl");
        QCOMPARE(a.last(), QString("https://x.com/v"));
        QCOMPARE(a.at(a.size() - 2), QString("--"));
        const int p = a.indexOf("-P");
        QVERIFY(p >= 0);
        QCOMPARE(a.at(p + 1), QString("/tmp/dl"));
        QVERIFY(a.contains("--no-playlist"));
    }
    void startProgram_runsArgvWithoutShell() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
#ifdef Q_OS_WIN
        QVERIFY(r.startProgram("cmd.exe", {"/c", "echo", "$(whoami)"}, "echo test"));
#else
        QVERIFY(r.startProgram("/bin/echo", {"$(whoami)", "a;b"}, "echo test"));
#endif
        QCOMPARE(r.command(), QString("echo test"));
        QVERIFY(done.wait(5000));
        QCOMPARE(r.exitCode(), 0);
        // The argument reaches the program verbatim: nothing was expanded
        QVERIFY(r.output().contains("$(whoami)"));
    }
    void startProgram_missingProgramReportsFailure() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.startProgram("ravenspot-no-such-program-xyz", {}, "missing"));
        // Windows reports FailedToStart synchronously inside start(), Linux a moment later
        QVERIFY(done.count() > 0 || done.wait(5000));
        QCOMPARE(r.exitCode(), 127);
        QVERIFY(r.output().contains("failed to start"));
    }
};

QObject *makeTestVideoDownload() { return new TestVideoDownload; }
#include "tst_videodownload.moc"
