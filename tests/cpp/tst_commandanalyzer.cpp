#include <QtTest>
#include "run/CommandAnalyzer.hpp"

using namespace ravenspot::cmd;

namespace {
bool fakeExec(const QString &name) {
    static const QStringList known = {"ls", "git", "firefox", "pacman", "echo", "rm", "dd"};
    return known.contains(name);
}
} // namespace

class TestCommandAnalyzer : public QObject {
    Q_OBJECT
private slots:
    void detect_bareWordIsNone() {
        QCOMPARE(detect("firefox", fakeExec), Signal::None);
        QCOMPARE(detect("   ", fakeExec), Signal::None);
    }
    void detect_execWithArgsIsWeak() {
        QCOMPARE(detect("firefox --private-window", fakeExec), Signal::Weak);
        QCOMPARE(detect("git status", fakeExec), Signal::Weak);
        QCOMPARE(detect("unknowncmd foo", fakeExec), Signal::None);
    }
    void detect_strongSignals() {
        for (const QString &t : {QStringLiteral("sudo pacman -Syu"), QStringLiteral("ls | head"),
                                 QStringLiteral("a && b"), QStringLiteral("echo a\necho b"),
                                 QStringLiteral("./run.sh"), QStringLiteral("~/bin/x"),
                                 QStringLiteral("echo $(date)")}) {
            QVERIFY2(detect(t, fakeExec) == Signal::Strong, qPrintable(t));
        }
    }
    void danger_matches() {
        for (const QString &t : {QStringLiteral("rm -rf /"), QStringLiteral("sudo rm -rf /*"),
                                 QStringLiteral("rm -rf ~"), QStringLiteral("rm -rf $HOME"),
                                 QStringLiteral("mkfs.ext4 /dev/sda1"),
                                 QStringLiteral("dd if=/dev/zero of=/dev/nvme0n1"),
                                 QStringLiteral("echo x > /dev/sda"), QStringLiteral(":(){ :|:& };:"),
                                 QStringLiteral("chmod -R 777 /"), QStringLiteral("sudo chown -R me /")}) {
            QVERIFY2(isDangerous(t), qPrintable(t));
        }
    }
    void danger_noFalsePositives() {
        for (const QString &t : {QStringLiteral("rm -rf ~/tmp/x"), QStringLiteral("rm -rf ./build"),
                                 QStringLiteral("dd if=a of=b.img"), QStringLiteral("chmod -R 755 ~/proj"),
                                 QStringLiteral("ls /dev/sda")}) {
            QVERIFY2(!isDangerous(t), qPrintable(t));
        }
    }
    void needsTerminal_cases() {
        QVERIFY(needsTerminal("sudo ls"));
        QVERIFY(needsTerminal("htop"));
        QVERIFY(needsTerminal("ssh host"));
        QVERIFY(needsTerminal("journalctl -f"));
        QVERIFY(!needsTerminal("ls -la"));
        QVERIFY(!needsTerminal("journalctl -b"));
    }
    void argv_preservesSpecialChars() {
        const QString cmd = QStringLiteral("echo '$x' \"$(date)\" `id` \\n\nprintf ok");
        const QStringList expectedTerm = {
            QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
            cmd + QStringLiteral("\nprintf \"\\n[exit %s] \" $?; exec \"${SHELL:-bash}\" -i")};
        QCOMPARE(terminalArgs(cmd), expectedTerm);
        QCOMPARE(inlineArgs(cmd), (QStringList{QStringLiteral("-c"), cmd}));
    }
    void lines_firstAndExtra() {
        QCOMPARE(firstLine("a\n\nb\nc"), QStringLiteral("a"));
        QCOMPARE(extraLineCount("a\n\nb\nc"), 2);
    }
    void analyze_map() {
        const QVariantMap a = analyze("sudo rm -rf /", fakeExec);
        QCOMPARE(a.value("signal").toInt(), 2);
        QCOMPARE(a.value("dangerous").toBool(), true);
        QCOMPARE(a.value("needsTerminal").toBool(), true);
        const QVariantMap b = analyze("ls -la", fakeExec);
        QCOMPARE(b.value("signal").toInt(), 1);
        QCOMPARE(b.value("dangerous").toBool(), false);
        QCOMPARE(b.value("extraLines").toInt(), 0);
    }
};

QObject *makeTestCommandAnalyzer() { return new TestCommandAnalyzer; }
#include "tst_commandanalyzer.moc"
