#include <QtTest>
#include <QProcess>
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
    void danger_reviewFalseNegatives() {
        for (const QString &t : {QStringLiteral("rm -rf ~/*"), QStringLiteral("rm -rf $HOME/*"),
                                 QStringLiteral("rm -rf ${HOME}"), QStringLiteral("rm --recursive --force /"),
                                 QStringLiteral("rm -rf /home/dokuro"), QStringLiteral("rm -rf /usr"),
                                 QStringLiteral("rm -rf build /"), QStringLiteral("rm -rf *"),
                                 QStringLiteral("cd ~ && rm -rf ."), QStringLiteral("rm -fr /"),
                                 QStringLiteral("rm -r -f /"), QStringLiteral("sudo  rm -rf /"),
                                 QStringLiteral("rm -rf / --no-preserve-root"), QStringLiteral("rm -rf \"$HOME\""),
                                 QStringLiteral("rm -rf /etc/")}) {
            QVERIFY2(isDangerous(t), qPrintable(t));
        }
        for (const QString &t : {QStringLiteral("rm -rf ./build"), QStringLiteral("rm -rf ~/tmp/x"),
                                 QStringLiteral("rm -rf *.o"), QStringLiteral("rm -rf /tmp/foo"),
                                 QStringLiteral("rm -rf /home/dokuro/tmp"), QStringLiteral("rm file /"),
                                 QStringLiteral("dd if=x of=/dev/null")}) {
            QVERIFY2(!isDangerous(t), qPrintable(t));
        }
    }
    void detect_singlePathTokenIsWeak() {
        QCOMPARE(detect("/usr/share", fakeExec), Signal::Weak);
        QCOMPARE(detect("~/Downloads", fakeExec), Signal::Weak);
        QCOMPARE(detect("./run.sh --fast", fakeExec), Signal::Strong);
    }
    void terminal_survivesSyntaxError() {
        // Kapanmamış tırnak: bash -c ayrıştırma hatasında bile çıkış satırına ulaşılmalı
        QStringList args = terminalArgs(QStringLiteral("echo \"foo"));
        QCOMPARE(args.takeFirst(), QStringLiteral("-e"));
        const QString program = args.takeFirst();
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SHELL", "/bin/true");
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(program, args);
        QVERIFY(p.waitForFinished(3000));
        QVERIFY2(p.readAll().contains("[exit"), "terminal script must reach the exit line");
    }
    void detect_strongSignals() {
        for (const QString &t : {QStringLiteral("sudo pacman -Syu"), QStringLiteral("ls | head"),
                                 QStringLiteral("a && b"), QStringLiteral("echo a\necho b"),
                                 QStringLiteral("./run.sh -v"), QStringLiteral("~/bin/x --y"),
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
            QStringLiteral("eval \"$1\"; printf \"\\n[exit %s] \" $?; exec \"${SHELL:-bash}\" -i"),
            QStringLiteral("ravenspot"), cmd};
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
