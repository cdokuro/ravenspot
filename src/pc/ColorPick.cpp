#include "ColorPick.hpp"

#include <algorithm>
#include <cmath>

namespace ravenspot::pc {

namespace {
int channel(double v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}
} // namespace

QString colorHex(double r, double g, double b) {
    return QStringLiteral("#%1%2%3")
        .arg(channel(r), 2, 16, QLatin1Char('0'))
        .arg(channel(g), 2, 16, QLatin1Char('0'))
        .arg(channel(b), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString colorRgb(double r, double g, double b) {
    return QStringLiteral("rgb(%1, %2, %3)").arg(channel(r)).arg(channel(g)).arg(channel(b));
}

} // namespace ravenspot::pc
