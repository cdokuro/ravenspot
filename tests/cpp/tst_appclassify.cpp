#include <QtTest>
#include "search/AppClassify.hpp"
#include "ui/IconAlias.hpp"

using namespace ravenspot;

// Kullanıcı uygulaması olmayan girdiler "Other Apps"e düşer;
// Edna (kora) temasında kora'da olmayan ikon adları kora karşılığına eşlenir.
class TestAppClassify : public QObject {
    Q_OBJECT
private slots:
    void kcmIsOther() {
        QVERIFY(isNonUserApp(QStringLiteral("kcm_kded"), QStringLiteral("Arka Plan Hizmetleri"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("kcm_notifications"), QStringLiteral("Bildirimler"), QString()));
    }
    void helpersAreOther() {
        QVERIFY(isNonUserApp(QStringLiteral("Uninstall Aula  GAMING MOUSE"), QStringLiteral("Uninstall Aula  GAMING MOUSE"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("ArctisManagerSystray"), QStringLiteral("Arctis Manager (system tray)"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("qv4l2"), QStringLiteral("Qt V4L2 test Utility"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("jshell-java21-openjdk"), QStringLiteral("OpenJDK Java 21 Shell"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("kdesystemsettings"), QStringLiteral("KDE Sistem Ayarları"), QString()));
        QVERIFY(isNonUserApp(QStringLiteral("org.kde.kdeconnect.nonplasma"), QStringLiteral("KDE Bağlan Göstergesi"), QString()));
    }
    void realAppsStay() {
        QVERIFY(!isNonUserApp(QStringLiteral("firefox"), QStringLiteral("Firefox"), QStringLiteral("Network;WebBrowser;")));
        QVERIFY(!isNonUserApp(QStringLiteral("systemsettings"), QStringLiteral("Sistem Ayarları"), QStringLiteral("Qt;KDE;Settings;")));
        QVERIFY(!isNonUserApp(QStringLiteral("org.kde.dolphin"), QStringLiteral("Dolphin"), QString()));
        QVERIFY(!isNonUserApp(QStringLiteral("designer"), QStringLiteral("Qt Widgets Designer"), QString()));
        QVERIFY(!isNonUserApp(QStringLiteral("ArctisManager"), QStringLiteral("Arctis Manager"), QString()));
    }
    void koraAliases() {
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("cachy-update-blue")), QStringLiteral("system-software-update"));
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("preferences-desktop-keyboard-shortcut")), QStringLiteral("preferences-desktop-keyboard"));
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("input-keyboard-virtual")), QStringLiteral("input-keyboard"));
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("preferences-desktop-touchscreen")), QStringLiteral("input-touchscreen"));
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("firefox")), QStringLiteral("firefox"));
    }
    void otherThemesUntouched() {
        QCOMPARE(iconAliasFor(QStringLiteral("candy-icons"), QStringLiteral("cachy-update-blue")), QStringLiteral("cachy-update-blue"));
        // ortak indirme eşlemesi tüm temalarda geçerli
        QCOMPARE(iconAliasFor(QStringLiteral("candy-icons"), QStringLiteral("folder-download")), QStringLiteral("aptdaemon-download"));
        QCOMPARE(iconAliasFor(QStringLiteral("kora"), QStringLiteral("system-lock")), QStringLiteral("system-lock-screen"));
    }
};

QObject *makeTestAppClassify() { return new TestAppClassify; }
#include "tst_appclassify.moc"
