#include <QtTest>
#include <windows.h>
#include "core/GlobalHotkey_win.hpp"
#include "run/CommandRunner.hpp"

using namespace ravenspot;

// Windows-only pieces that can run headless on CI (no desktop interaction).
class TestWinPlatform : public QObject {
    Q_OBJECT
private slots:
    void hotkey_parsesSpecs() {
        unsigned int mods = 0, vk = 0;
        QVERIFY(GlobalHotkey::parseSpec("alt+space", &mods, &vk));
        QCOMPARE(mods, static_cast<unsigned int>(MOD_ALT));
        QCOMPARE(vk, static_cast<unsigned int>(VK_SPACE));
        QVERIFY(GlobalHotkey::parseSpec("Ctrl + Alt + Space", &mods, &vk));
        QCOMPARE(mods, static_cast<unsigned int>(MOD_CONTROL | MOD_ALT));
        QVERIFY(GlobalHotkey::parseSpec("win+shift+r", &mods, &vk));
        QCOMPARE(vk, static_cast<unsigned int>('R'));
        QVERIFY(GlobalHotkey::parseSpec("ctrl+f12", &mods, &vk));
        QCOMPARE(vk, static_cast<unsigned int>(VK_F12));
    }
    void hotkey_rejectsBadSpecs() {
        QVERIFY(!GlobalHotkey::parseSpec("", nullptr, nullptr));
        QVERIFY(!GlobalHotkey::parseSpec("space", nullptr, nullptr));       // no modifier
        QVERIFY(!GlobalHotkey::parseSpec("alt+", nullptr, nullptr));
        QVERIFY(!GlobalHotkey::parseSpec("alt+banana", nullptr, nullptr));
        QVERIFY(!GlobalHotkey::parseSpec("space+alt", nullptr, nullptr));   // key must be last
        QVERIFY(!GlobalHotkey::parseSpec("ctrl+f25", nullptr, nullptr));
    }
    void runner_powershellOutputAndExitCode() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        QVERIFY(r.start("Write-Output 'hi'; Write-Output 'ğ'; exit 3"));
        QVERIFY(done.wait(20000)); // PowerShell cold start on CI runners is slow
        QCOMPARE(r.exitCode(), 3);
        QVERIFY(r.output().contains("hi"));
        QVERIFY(r.output().contains(QString::fromUtf8("\xc4\x9f"))); // UTF-8 pipe
    }
    void runner_stopEndsProcessTree() {
        CommandRunner r;
        QSignalSpy done(&r, &CommandRunner::finished);
        // A child process of the shell must die with it (Job object)
        QVERIFY(r.start("Start-Process -NoNewWindow -Wait ping.exe -ArgumentList '-n','60','127.0.0.1'"));
        QTest::qWait(3000);
        r.stop();
        QVERIFY(done.wait(5000));
        QVERIFY(!r.isRunning());
    }
};

QObject *makeTestWinPlatform() { return new TestWinPlatform; }
#include "tst_winplatform.moc"
