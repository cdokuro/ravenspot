#pragma once

#include <QString>

namespace ravenspot::pc {

// xdg-desktop-portal PickColor sonucu (0..1 aralığında r, g, b) metne:
// colorHex → "#10A6D4", colorRgb → "rgb(16, 166, 212)". Aralık dışı kırpılır.
[[nodiscard]] QString colorHex(double r, double g, double b);
[[nodiscard]] QString colorRgb(double r, double g, double b);

} // namespace ravenspot::pc
