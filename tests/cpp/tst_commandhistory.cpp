#include <QtTest>
#include <QTemporaryDir>
#include "run/CommandHistory.hpp"

using ravenspot::CommandHistory;

class TestCommandHistory : public QObject {
    Q_OBJECT
private slots:
    void add_dedupMovesToFront() {
        QTemporaryDir d;
        CommandHistory h(d.filePath("h.json"));
        h.add("ls"); h.add("git status"); h.add("ls");
        QCOMPARE(h.entries(), (QStringList{"ls", "git status"}));
    }
    void add_capsAt50() {
        QTemporaryDir d;
        CommandHistory h(d.filePath("h.json"));
        for (int i = 0; i < 60; ++i) h.add(QStringLiteral("cmd %1").arg(i));
        QCOMPARE(h.entries().size(), CommandHistory::kMaxEntries);
        QCOMPARE(h.entries().first(), QStringLiteral("cmd 59"));
    }
    void persist_roundTrip() {
        QTemporaryDir d;
        { CommandHistory h(d.filePath("sub/h.json")); h.add("echo a"); h.add("echo b"); }
        CommandHistory again(d.filePath("sub/h.json"));
        QCOMPARE(again.entries(), (QStringList{"echo b", "echo a"}));
    }
    void matching_caseInsensitiveNewestFirst() {
        QTemporaryDir d;
        CommandHistory h(d.filePath("h.json"));
        h.add("Git log"); h.add("ls"); h.add("git status");
        QCOMPARE(h.matching("GIT"), (QStringList{"git status", "Git log"}));
        QCOMPARE(h.matching("git", 1), (QStringList{"git status"}));
    }
    void load_corruptFileIsEmpty() {
        QTemporaryDir d;
        QFile f(d.filePath("h.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{garbage");
        f.close();
        CommandHistory h(d.filePath("h.json"));
        QVERIFY(h.entries().isEmpty());
    }
    void persist_ownerOnlyPermissions() {
        // Geçmişte token/parola olabilir: yalnızca sahibi okuyabilmeli (0600)
        QTemporaryDir d;
        CommandHistory h(d.filePath("h.json"));
        h.add("curl -H 'Authorization: Bearer x'");
        const auto perms = QFileInfo(d.filePath("h.json")).permissions();
        QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
        QVERIFY(!perms.testFlag(QFileDevice::ReadGroup));
        QVERIFY(!perms.testFlag(QFileDevice::ReadOther));
    }
    void add_ignoresBlank() {
        QTemporaryDir d;
        CommandHistory h(d.filePath("h.json"));
        h.add("   "); h.add("  ls  ");
        QCOMPARE(h.entries(), (QStringList{"ls"}));
    }
};

QObject *makeTestCommandHistory() { return new TestCommandHistory; }
#include "tst_commandhistory.moc"
