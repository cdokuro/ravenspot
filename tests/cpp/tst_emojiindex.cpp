#include <QtTest>
#include <QTemporaryDir>
#include "pc/EmojiIndex.hpp"

using ravenspot::EmojiIndex;

namespace {
QString fixture(const QString &dir) {
    const QString p = dir + "/emoji.json";
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QString::fromUtf8(
            "[[\"😀\",\"grinning face\",\"grinning\",\"smile happy\"],"
            "[\"❤️\",\"red heart\",\"heart\",\"love\"],"
            "[\"😍\",\"smiling face with heart-eyes\",\"heart_eyes\",\"love crush\"],"
            "[\"👍\",\"thumbs up\",\"+1 thumbsup\",\"approve ok\"],"
            "[\"💔\",\"broken heart\",\"broken_heart\",\"\"]]").toUtf8());
    }
    return p;
}
QStringList glyphs(const QVariantList &l) { QStringList o; for (const auto &v : l) o << v.toMap().value("glyph").toString(); return o; }
} // namespace

class TestEmojiIndex : public QObject {
    Q_OBJECT
private slots:
    void search_exactAliasFirst() {
        QTemporaryDir d; EmojiIndex e; QVERIFY(e.load(fixture(d.path())));
        const QStringList g = glyphs(e.search("heart"));
        QCOMPARE(g.first(), QString::fromUtf8("❤️"));
        QVERIFY(g.contains(QString::fromUtf8("😍")));
        QVERIFY(g.contains(QString::fromUtf8("💔")));
    }
    void search_prefix() {
        QTemporaryDir d; EmojiIndex e; QVERIFY(e.load(fixture(d.path())));
        QCOMPARE(glyphs(e.search("thumb")).first(), QString::fromUtf8("👍"));
        QCOMPARE(glyphs(e.search("+1")).first(), QString::fromUtf8("👍"));
    }
    void search_tagMatch() {
        QTemporaryDir d; EmojiIndex e; QVERIFY(e.load(fixture(d.path())));
        QCOMPARE(glyphs(e.search("happy")), (QStringList{QString::fromUtf8("😀")}));
        QCOMPARE(e.search("happy").first().toMap().value("name").toString(), QStringLiteral("grinning face"));
    }
    void search_emptyIsTop() {
        QTemporaryDir d; EmojiIndex e; QVERIFY(e.load(fixture(d.path())));
        QCOMPARE(e.search("", 2).size(), 2);
        QCOMPARE(glyphs(e.search("", 2)).first(), QString::fromUtf8("😀"));
    }
    void load_missingFileFalse() {
        EmojiIndex e;
        QVERIFY(!e.load("/definitely/missing.json"));
        QVERIFY(e.search("heart").isEmpty());
    }
    void load_realAsset() {
        EmojiIndex e;
        QVERIFY(e.load(QStringLiteral(SRCDIR "/../../assets/emoji.json")));
        QCOMPARE(glyphs(e.search("+1", 1)).first(), QString::fromUtf8("👍"));
    }
};

QObject *makeTestEmojiIndex() { return new TestEmojiIndex; }
#include "tst_emojiindex.moc"
