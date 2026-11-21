#include <QtTest>
#include "pc/ColorPick.hpp"

using namespace ravenspot::pc;

// Portal PickColor rengi 0..1 aralığında (r, g, b) double olarak verir.
class TestColorPick : public QObject {
    Q_OBJECT
private slots:
    void hexIsUppercaseSixDigits() {
        QCOMPARE(colorHex(1.0, 0.0, 0.0), QStringLiteral("#FF0000"));
        QCOMPARE(colorHex(16 / 255.0, 166 / 255.0, 212 / 255.0), QStringLiteral("#10A6D4"));
        QCOMPARE(colorHex(0.0, 0.0, 0.0), QStringLiteral("#000000"));
    }
    void roundsToNearest() {
        QCOMPARE(colorHex(0.5, 0.5, 0.5), QStringLiteral("#808080"));
    }
    void clampsOutOfRange() {
        QCOMPARE(colorHex(1.2, -0.1, 0.0), QStringLiteral("#FF0000"));
    }
    void rgbText() {
        QCOMPARE(colorRgb(16 / 255.0, 166 / 255.0, 212 / 255.0), QStringLiteral("rgb(16, 166, 212)"));
    }
};

QObject *makeTestColorPick() { return new TestColorPick; }
#include "tst_colorpick.moc"
