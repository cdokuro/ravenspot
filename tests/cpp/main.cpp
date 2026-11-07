#include <QtTest>
#include <memory>

// Tek test programı: her test sınıfı sırayla çalışır, toplam hata sayısı döner.
QObject *makeTestCommandAnalyzer();
QObject *makeTestCommandRunner();
QObject *makeTestCommandHistory();
QObject *makeTestFileActions();
QObject *makeTestClipboardHistory();
QObject *makeTestProcessList();
QObject *makeTestRecentFiles();
QObject *makeTestEmojiIndex();
QObject *makeTestVideoDownload();
QObject *makeTestAppClassify();
#ifdef Q_OS_WIN
QObject *makeTestWinPlatform();
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
#ifdef Q_OS_WIN
    // bash-based CommandRunner tests are Linux-only; tst_winplatform covers PowerShell
    for (auto make : {&makeTestCommandAnalyzer, &makeTestWinPlatform, &makeTestCommandHistory, &makeTestFileActions, &makeTestClipboardHistory, &makeTestProcessList, &makeTestRecentFiles, &makeTestEmojiIndex, &makeTestVideoDownload, &makeTestAppClassify}) {
#else
    for (auto make : {&makeTestCommandAnalyzer, &makeTestCommandRunner, &makeTestCommandHistory, &makeTestFileActions, &makeTestClipboardHistory, &makeTestProcessList, &makeTestRecentFiles, &makeTestEmojiIndex, &makeTestVideoDownload, &makeTestAppClassify}) {
#endif
        std::unique_ptr<QObject> t(make());
        failures += QTest::qExec(t.get(), argc, argv);
    }
    return failures;
}
