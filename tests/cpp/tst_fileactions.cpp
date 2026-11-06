#include <QtTest>
#include <QTemporaryDir>
#include "files/FileActions.hpp"

using namespace ravenspot::files;

namespace {
void touch(const QString &p, const QByteArray &data = "x") {
    QFile f(p); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(data);
}
QString writeDesktop(const QString &dir, const QString &name, const QString &body) {
    const QString p = dir + "/" + name;
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) f.write(body.toUtf8());
    return p;
}
} // namespace

class TestFileActions : public QObject {
    Q_OBJECT
private slots:
    void rename_ok() {
        QTemporaryDir d; touch(d.filePath("a.txt"));
        QCOMPARE(renamePath(d.filePath("a.txt"), "b.txt"), QString());
        QVERIFY(QFile::exists(d.filePath("b.txt")));
        QVERIFY(!QFile::exists(d.filePath("a.txt")));
    }
    void rename_rejectsExisting() {
        QTemporaryDir d; touch(d.filePath("a.txt")); touch(d.filePath("b.txt"));
        QVERIFY(!renamePath(d.filePath("a.txt"), "b.txt").isEmpty());
        QVERIFY(QFile::exists(d.filePath("a.txt")));
    }
    void rename_rejectsSlash() {
        QTemporaryDir d; touch(d.filePath("a.txt"));
        QVERIFY(!renamePath(d.filePath("a.txt"), "x/y").isEmpty());
        QVERIFY(QFile::exists(d.filePath("a.txt")));
    }
    void rename_rejectsDots() {
        QTemporaryDir d; touch(d.filePath("a.txt"));
        QVERIFY(!renamePath(d.filePath("a.txt"), "..").isEmpty());
        QVERIFY(!renamePath(d.filePath("a.txt"), ".").isEmpty());
    }
    void rename_rejectsEmpty() {
        QTemporaryDir d; touch(d.filePath("a.txt"));
        QVERIFY(!renamePath(d.filePath("a.txt"), "   ").isEmpty());
    }
    void properties_fileAndDir() {
        QTemporaryDir d; touch(d.filePath("f.txt"), "hello");
        QDir(d.path()).mkdir("sub"); touch(d.filePath("sub/1")); touch(d.filePath("sub/2"));
        const QVariantMap f = properties(d.filePath("f.txt"));
        QCOMPARE(f.value("name").toString(), QStringLiteral("f.txt"));
        QCOMPARE(f.value("size").toLongLong(), 5LL);
        QCOMPARE(f.value("isDir").toBool(), false);
        QCOMPARE(f.value("mime").toString(), QStringLiteral("text/plain"));
        QCOMPARE(f.value("permissions").toString().size(), 9);
        const QVariantMap s = properties(d.filePath("sub"));
        QCOMPARE(s.value("isDir").toBool(), true);
        QCOMPARE(s.value("items").toInt(), 2);
    }
    void isArchive_detects() {
        QTemporaryDir d;
        // minimal zip local header signature
        touch(d.filePath("a.zip"), QByteArray("PK\x03\x04", 4) + QByteArray(30, '\0'));
        touch(d.filePath("b.txt"), "plain");
        QVERIFY(isArchive(d.filePath("a.zip")));
        QVERIFY(!isArchive(d.filePath("b.txt")));
    }
    void terminalInDir_argvKeepsPath() {
        const QString dir = QStringLiteral("/tmp/a b/\"q\"/$(id)");
        const QStringList a = terminalInDirArgs(dir);
        QCOMPARE(a.size(), 6);
        QCOMPARE(a.at(0), QStringLiteral("-e"));
        QCOMPARE(a.at(1), QStringLiteral("bash"));
        QCOMPARE(a.last(), dir);
    }
    void parseDesktop_readsMimeAndName() {
        QTemporaryDir d;
        const QString p = writeDesktop(d.path(), "kate.desktop",
            "[Desktop Entry]\nName=Kate\nName[tr]=Kate TR\nIcon=kate\nMimeType=text/plain;text/x-c++src;\n"
            "[Desktop Action new]\nName=New Window\n");
        const DesktopApp a = parseDesktopFile(p);
        QCOMPARE(a.name, QStringLiteral("Kate"));
        QCOMPARE(a.icon, QStringLiteral("kate"));
        QCOMPARE(a.mimeTypes, (QStringList{"text/plain", "text/x-c++src"}));
        QVERIFY(!a.noDisplay);
    }
    void openWith_matchesExactAndDefaultFirst() {
        QTemporaryDir d; touch(d.filePath("n.txt"), "hello");
        QList<DesktopApp> apps = {
            {"/x/zed.desktop", "Zed", "zed", {"text/plain"}, false},
            {"/x/kate.desktop", "Kate", "kate", {"text/plain"}, false},
            {"/x/gimp.desktop", "GIMP", "gimp", {"image/png"}, false},
            {"/x/hidden.desktop", "Hidden", "", {"text/plain"}, false, true}};
        const QVariantList r = openWithApps(d.filePath("n.txt"), apps, "zed.desktop");
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0).toMap().value("name").toString(), QStringLiteral("Zed"));
        QCOMPARE(r.at(0).toMap().value("isDefault").toBool(), true);
        QCOMPARE(r.at(1).toMap().value("name").toString(), QStringLiteral("Kate"));
    }
    void openWith_matchesAncestorMime() {
        QTemporaryDir d; touch(d.filePath("main.cpp"), "int main(){}\n");
        QList<DesktopApp> apps = {{"/x/kate.desktop", "Kate", "kate", {"text/plain"}, false}};
        QCOMPARE(openWithApps(d.filePath("main.cpp"), apps, QString()).size(), 1);
    }
    void autostart_roundTrip() {
        QTemporaryDir d; QTemporaryDir apps;
        qputenv("RAVENSPOT_AUTOSTART_DIR", d.path().toUtf8());
        const QString desk = writeDesktop(apps.path(), "foo.desktop", "[Desktop Entry]\nName=Foo\nExec=foo\n");
        QVERIFY(!isAutostart(desk));
        QVERIFY(setAutostart(desk, true));
        QVERIFY(isAutostart(desk));
        QVERIFY(QFile::exists(d.filePath("foo.desktop")));
        QVERIFY(setAutostart(desk, false));
        QVERIFY(!isAutostart(desk));
        qunsetenv("RAVENSPOT_AUTOSTART_DIR");
    }
    void uninstall_flatpakAndPacman() {
        auto owner = [](const QString &f) { return f.startsWith("/usr/share") ? QStringLiteral("kate") : QString(); };
        QCOMPARE(uninstallCommand("/var/lib/flatpak/exports/share/applications/org.gimp.GIMP.desktop", owner),
                 QStringLiteral("flatpak uninstall org.gimp.GIMP"));
        QCOMPARE(uninstallCommand("/usr/share/applications/org.kde.kate.desktop", owner),
                 QStringLiteral("sudo pacman -Rns kate"));
    }
    void uninstall_noOwnerIsEmpty() {
        auto owner = [](const QString &) { return QString(); };
        QCOMPARE(uninstallCommand(QDir::homePath() + "/.local/share/applications/my.desktop", owner), QString());
    }
    void rename_trailingSlashDir() {
        // fd klasörleri sonda "/" ile verir
        QTemporaryDir d; QDir(d.path()).mkdir("sub");
        QCOMPARE(renamePath(d.filePath("sub") + "/", "renamed"), QString());
        QVERIFY(QFileInfo(d.filePath("renamed")).isDir());
    }
    void properties_trailingSlashDir() {
        QTemporaryDir d; QDir(d.path()).mkdir("sub");
        QCOMPARE(properties(d.filePath("sub") + "/").value("name").toString(), QStringLiteral("sub"));
    }
    void openWith_includesNoDisplayHandler() {
        QTemporaryDir d; touch(d.filePath("n.txt"), "hello");
        QList<DesktopApp> apps = {{"/x/fontinst.desktop", "Font Installer", "", {"text/plain"}, true, false},
                                  {"/x/gone.desktop", "Gone", "", {"text/plain"}, false, true}};
        const QVariantList r = openWithApps(d.filePath("n.txt"), apps, "fontinst.desktop");
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.at(0).toMap().value("isDefault").toBool(), true);
    }
    void parseDesktop_hiddenVsNoDisplay() {
        QTemporaryDir d;
        const DesktopApp a = parseDesktopFile(writeDesktop(d.path(), "a.desktop", "[Desktop Entry]\nName=A\nNoDisplay=true\n"));
        const DesktopApp b = parseDesktopFile(writeDesktop(d.path(), "b.desktop", "[Desktop Entry]\nName=B\nHidden=true\n"));
        QVERIFY(a.noDisplay && !a.hidden);
        QVERIFY(b.hidden);
    }
    void terminalDir_resolves() {
        QTemporaryDir d; touch(d.filePath("f.txt"));
        const QString home = d.path();
        QCOMPARE(resolveTerminalDir("~", home), home);
        QCOMPARE(resolveTerminalDir("", home), home);
        QCOMPARE(resolveTerminalDir("/definitely/missing/dir", home), home);
        QCOMPARE(resolveTerminalDir(d.filePath("f.txt"), home), QDir::cleanPath(d.path()));
        QDir(d.path()).mkdir("w");
        QCOMPARE(resolveTerminalDir("~/w", home), QDir::cleanPath(d.filePath("w")));
    }
};

QObject *makeTestFileActions() { return new TestFileActions; }
#include "tst_fileactions.moc"
