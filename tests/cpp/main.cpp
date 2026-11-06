#include <QtTest>
#include <memory>

// Tek test programı: her test sınıfı sırayla çalışır, toplam hata sayısı döner.
QObject *makeTestCommandAnalyzer();
QObject *makeTestCommandRunner();

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    for (auto make : {&makeTestCommandAnalyzer, &makeTestCommandRunner}) {
        std::unique_ptr<QObject> t(make());
        failures += QTest::qExec(t.get(), argc, argv);
    }
    return failures;
}
