#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include "pc/ClipboardHistory.hpp"

using ravenspot::ClipboardHistory;
using ravenspot::ClipEntry;

namespace {
QByteArray ev(const QString &text, const QString &fp) {
    return QStringLiteral("{\"seq\":1,\"timestamp\":5,\"types\":[\"text/plain\"],\"text\":%1,\"html\":null,"
                          "\"image_path\":null,\"fingerprint\":\"%2\",\"origin\":\"wl\"}\n")
        .arg(text.isNull() ? QStringLiteral("null") : QStringLiteral("\"%1\"").arg(text), fp).toUtf8();
}
} // namespace

class TestClipboardHistory : public QObject {
    Q_OBJECT
private slots:
    void parse_textEvent() {
        const auto e = ClipboardHistory::parseEvent(ev("hello", "abc"));
        QVERIFY(e.has_value());
        QCOMPARE(e->text, QStringLiteral("hello"));
        QCOMPARE(e->fingerprint, QStringLiteral("abc"));
        QCOMPARE(e->timestamp, 5LL);
    }
    void parse_ignoresNonText() {
        QVERIFY(!ClipboardHistory::parseEvent(ev(QString(), "img")).has_value());
        QVERIFY(!ClipboardHistory::parseEvent(ev("   ", "ws")).has_value());
    }
    void parse_rejectsGarbage() {
        QVERIFY(!ClipboardHistory::parseEvent("{not json").has_value());
        QVERIFY(!ClipboardHistory::parseEvent("[1,2]").has_value());
    }
    void ingest_dedupByFingerprint() {
        ClipboardHistory h;
        h.ingest({"a", "f1", 1}); h.ingest({"b", "f2", 2}); h.ingest({"a", "f1", 3});
        QCOMPARE(h.entries().size(), 2);
        QCOMPARE(h.entries().first().text, QStringLiteral("a"));
    }
    void ingest_capsLengthAndCount() {
        ClipboardHistory h;
        h.ingest({QString(10000, 'x'), "big", 1});
        QCOMPARE(h.entries().first().text.size(), ClipboardHistory::kMaxChars);
        for (int i = 0; i < 70; ++i) h.ingest({QStringLiteral("t%1").arg(i), QStringLiteral("f%1").arg(i), i});
        QCOMPARE(h.entries().size(), ClipboardHistory::kMax);
        QCOMPARE(h.entries().first().text, QStringLiteral("t69"));
    }
    void matching_caseInsensitive() {
        ClipboardHistory h;
        h.ingest({"Hello World", "1", 1}); h.ingest({"other", "2", 2});
        QCOMPARE(h.matching("hello", 5).size(), 1);
        QCOMPARE(h.matching("", 5).size(), 2);
    }
    void socket_receivesEvents() {
        QTemporaryDir d;
        const QString path = d.filePath("clip.sock");
        QLocalServer server;
        QVERIFY(server.listen(path));
        ClipboardHistory h;
        QSignalSpy spy(&h, &ClipboardHistory::changed);
        h.connectTo(path);
        QVERIFY(server.waitForNewConnection(2000));
        QLocalSocket *c = server.nextPendingConnection();
        c->write(ev("one", "f1") + ev("two", "f2"));
        c->flush();
        QTRY_COMPARE_WITH_TIMEOUT(h.entries().size(), 2, 2000);
        QCOMPARE(h.entries().first().text, QStringLiteral("two"));
    }
    void socket_reconnects() {
        QTemporaryDir d;
        const QString path = d.filePath("clip.sock");
        auto *server = new QLocalServer;
        QVERIFY(server->listen(path));
        ClipboardHistory h;
        h.connectTo(path);
        QVERIFY(server->waitForNewConnection(2000));
        delete server; // daemon kapandı
        QTest::qWait(200);
        QLocalServer again;
        QVERIFY(again.listen(path)); // daemon yeniden başladı
        // waitForNewConnection olay döngüsünü bloke eder; istemcinin yeniden bağlanma zamanlayıcısı çalışsın
        QTRY_VERIFY_WITH_TIMEOUT(again.hasPendingConnections(), 4000);
        QLocalSocket *c = again.nextPendingConnection();
        c->write(ev("after", "f9"));
        c->flush();
        QTRY_COMPARE_WITH_TIMEOUT(h.entries().size(), 1, 2000);
    }
    void ingest_truncationMarkedAndKeepsSurrogates() {
        ClipboardHistory h;
        // kMaxChars-1 'x' + bir emoji (2 UTF-16 birimi): sınır vekil çiftin ortasına düşer
        const QString text = QString(ClipboardHistory::kMaxChars - 1, 'x') + QString::fromUtf8("\xF0\x9F\x91\x8D") + "tail";
        h.ingest({text, "t", 1});
        const ClipEntry e = h.entries().first();
        QVERIFY(e.truncated);
        QVERIFY(e.text.size() <= ClipboardHistory::kMaxChars);
        QVERIFY(!e.text.back().isHighSurrogate());
        h.ingest({"short", "s", 2});
        QVERIFY(!h.entries().first().truncated);
    }
};

QObject *makeTestClipboardHistory() { return new TestClipboardHistory; }
#include "tst_clipboardhistory.moc"
