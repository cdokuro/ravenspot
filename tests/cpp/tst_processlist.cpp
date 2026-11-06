#include <QtTest>
#include <QTemporaryDir>
#include "pc/ProcessList.hpp"

using ravenspot::pc::listProcesses;

namespace {
void addProc(const QString &root, int pid, const QString &comm, const QByteArray &cmdline, uint uid, int rssKb) {
    QDir(root).mkpath(QString::number(pid));
    const QString d = root + "/" + QString::number(pid) + "/";
    QFile c(d + "comm"); (void)c.open(QIODevice::WriteOnly); c.write(comm.toUtf8() + "\n"); c.close();
    QFile l(d + "cmdline"); (void)l.open(QIODevice::WriteOnly); l.write(cmdline); l.close();
    QFile s(d + "status"); (void)s.open(QIODevice::WriteOnly);
    s.write(QStringLiteral("Name:\t%1\nUid:\t%2\t%2\t%2\t%2\nVmRSS:\t   %3 kB\n").arg(comm).arg(uid).arg(rssKb).toUtf8());
    s.close();
}
QStringList names(const QVariantList &l) {
    QStringList out; for (const auto &v : l) out << v.toMap().value("name").toString(); return out;
}
} // namespace

class TestProcessList : public QObject {
    Q_OBJECT
private slots:
    void list_matchesNameAndCmdline() {
        QTemporaryDir d;
        addProc(d.path(), 100, "firefox", QByteArray("/usr/lib/firefox/firefox\0-P\0", 28), 1000, 500);
        addProc(d.path(), 101, "python3", QByteArray("python3\0firefox_helper.py\0", 26), 1000, 50);
        addProc(d.path(), 102, "kate", QByteArray("kate\0", 5), 1000, 80);
        QCOMPARE(names(listProcesses("FIREFOX", d.path(), 1000)), (QStringList{"firefox", "python3"}));
    }
    void list_ownUidOnly() {
        QTemporaryDir d;
        addProc(d.path(), 200, "bash", QByteArray("bash\0", 5), 1000, 10);
        addProc(d.path(), 201, "bash", QByteArray("bash\0", 5), 0, 10);
        const QVariantList r = listProcesses("bash", d.path(), 1000);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.first().toMap().value("pid").toInt(), 200);
    }
    void list_excludesPid1() {
        QTemporaryDir d;
        addProc(d.path(), 1, "systemd", QByteArray("systemd\0", 8), 1000, 10);
        QVERIFY(listProcesses("systemd", d.path(), 1000).isEmpty());
    }
    void list_sortedByRss() {
        QTemporaryDir d;
        addProc(d.path(), 300, "a-small", QByteArray("a-small\0", 8), 1000, 10);
        addProc(d.path(), 301, "a-big", QByteArray("a-big\0", 6), 1000, 9000);
        const QVariantList r = listProcesses("a-", d.path(), 1000);
        QCOMPARE(names(r), (QStringList{"a-big", "a-small"}));
        QCOMPARE(r.first().toMap().value("rssKb").toInt(), 9000);
        QVERIFY(r.first().toMap().value("rssText").toString().contains("MB"));
    }
    void list_emptyQueryIsEmpty() {
        QTemporaryDir d;
        addProc(d.path(), 400, "x", QByteArray("x\0", 2), 1000, 10);
        QVERIFY(listProcesses("  ", d.path(), 1000).isEmpty());
    }
};

QObject *makeTestProcessList() { return new TestProcessList; }
#include "tst_processlist.moc"
