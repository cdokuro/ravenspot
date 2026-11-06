#include <QtTest>
#include <memory>

// Tek test programı: her test sınıfı sırayla çalışır, toplam hata sayısı döner.
QObject *makeTestCommandAnalyzer();
QObject *makeTestCommandRunner();
QObject *makeTestCommandHistory();
QObject *makeTestFileActions();
QObject *makeTestClipboardHistory();

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    for (auto make : {&makeTestCommandAnalyzer, &makeTestCommandRunner, &makeTestCommandHistory, &makeTestFileActions, &makeTestClipboardHistory}) {
        std::unique_ptr<QObject> t(make());
        failures += QTest::qExec(t.get(), argc, argv);
    }
    return failures;
}
