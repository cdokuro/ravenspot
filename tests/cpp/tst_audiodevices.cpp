#include <QtTest>
#include "pc/AudioDevices.hpp"

using namespace ravenspot::pc;

// pactl --format=json list sinks/sources çıktısından seçilebilir cihaz listesi:
// monitörler elenir, görünen ad description'dan gelir, varsayılan işaretlenir.
class TestAudioDevices : public QObject {
    Q_OBJECT
private slots:
    void parsesSinksAndMarksDefault() {
        const QByteArray json = R"([
          {"name":"alsa_output.usb-Arctis.analog-stereo","description":"SS Arctis Nova 5","properties":{"device.class":"sound"}},
          {"name":"bluez_output.66_2B.1","description":"THarmony","properties":{"device.class":"sound"}}
        ])";
        const auto list = parseAudioDevices(json, QStringLiteral("bluez_output.66_2B.1"));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].toMap().value("name").toString(), QStringLiteral("SS Arctis Nova 5"));
        QCOMPARE(list[0].toMap().value("id").toString(), QStringLiteral("alsa_output.usb-Arctis.analog-stereo"));
        QCOMPARE(list[0].toMap().value("isDefault").toBool(), false);
        QCOMPARE(list[1].toMap().value("isDefault").toBool(), true);
    }
    void dropsMonitorSources() {
        const QByteArray json = R"([
          {"name":"alsa_output.x.analog-stereo.monitor","description":"Monitor of X","properties":{"device.class":"monitor"}},
          {"name":"alsa_input.x.mono","description":"Mic X","properties":{"device.class":"sound"}},
          {"name":"bluez_output.y.1.monitor","description":"Monitor of Y","properties":{}}
        ])";
        const auto list = parseAudioDevices(json, QStringLiteral("alsa_input.x.mono"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].toMap().value("name").toString(), QStringLiteral("Mic X"));
        QVERIFY(list[0].toMap().value("isDefault").toBool());
    }
    void fallsBackToNameWithoutDescription() {
        const auto list = parseAudioDevices(R"([{"name":"sink.a"}])", QString());
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].toMap().value("name").toString(), QStringLiteral("sink.a"));
    }
    void badJsonIsEmpty() {
        QVERIFY(parseAudioDevices("not json", QString()).isEmpty());
        QVERIFY(parseAudioDevices("{}", QString()).isEmpty());
    }
};

QObject *makeTestAudioDevices() { return new TestAudioDevices; }
#include "tst_audiodevices.moc"
