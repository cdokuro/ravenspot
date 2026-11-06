#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include "pc/RecentFiles.hpp"

using ravenspot::pc::recentFiles;

namespace {
QString bm(const QString &path, const QString &modified) {
    return QStringLiteral("  <bookmark href=\"%1\" added=\"2026-01-01T00:00:00Z\" modified=\"%2\" visited=\"%2\">\n"
                          "    <info><metadata owner=\"http://freedesktop.org\"><mime:mime-type type=\"text/plain\"/></metadata></info>\n"
                          "  </bookmark>\n").arg(QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded()), modified);
}
QString writeXbel(const QString &dir, const QStringList &bookmarks) {
    const QString p = dir + "/recent.xbel";
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) return p;
    f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\" "
            "xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\" "
            "xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n");
    for (const QString &b : bookmarks) f.write(b.toUtf8());
    f.write("</xbel>\n");
    return p;
}
void touch(const QString &p) { QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write("x"); }
QStringList names(const QVariantList &l) { QStringList o; for (const auto &v : l) o << v.toMap().value("name").toString(); return o; }
} // namespace

class TestRecentFiles : public QObject {
    Q_OBJECT
private slots:
    void recent_newestFirst() {
        QTemporaryDir d; touch(d.filePath("old.txt")); touch(d.filePath("new.txt"));
        const QString x = writeXbel(d.path(), {bm(d.filePath("old.txt"), "2026-01-01T10:00:00Z"),
                                               bm(d.filePath("new.txt"), "2026-02-01T10:00:00Z")});
        QCOMPARE(names(recentFiles(x, "")), (QStringList{"new.txt", "old.txt"}));
    }
    void recent_skipsMissing() {
        QTemporaryDir d; touch(d.filePath("here.txt"));
        const QString x = writeXbel(d.path(), {bm(d.filePath("gone.txt"), "2026-03-01T00:00:00Z"),
                                               bm(d.filePath("here.txt"), "2026-01-01T00:00:00Z")});
        QCOMPARE(names(recentFiles(x, "")), (QStringList{"here.txt"}));
    }
    void recent_decodesPercent() {
        QTemporaryDir d; QDir(d.path()).mkdir(QString::fromUtf8("İndirilenler"));
        const QString dir = d.filePath(QString::fromUtf8("İndirilenler"));
        const QString x = writeXbel(d.path(), {bm(dir, "2026-01-01T00:00:00Z")});
        const QVariantList r = recentFiles(x, "");
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.first().toMap().value("path").toString(), dir);
        QCOMPARE(r.first().toMap().value("isDir").toBool(), true);
    }
    void recent_filtersQuery() {
        QTemporaryDir d; touch(d.filePath("Report.pdf")); touch(d.filePath("photo.png"));
        const QString x = writeXbel(d.path(), {bm(d.filePath("Report.pdf"), "2026-01-01T00:00:00Z"),
                                               bm(d.filePath("photo.png"), "2026-01-02T00:00:00Z")});
        QCOMPARE(names(recentFiles(x, "report")), (QStringList{"Report.pdf"}));
    }
    void recent_missingFileIsEmpty() {
        QVERIFY(recentFiles("/definitely/missing.xbel", "").isEmpty());
    }
};

QObject *makeTestRecentFiles() { return new TestRecentFiles; }
#include "tst_recentfiles.moc"
