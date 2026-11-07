#pragma once

#include <QString>

namespace ravenspot {

// Bir ikon adını etkin ikon temasında var olan karşılığına çevirir. Ortak
// eşlemeler her temada, temaya özel eşlemeler (ör. kora/Edna'da eksik
// sistem adları) yalnız o temada uygulanır; bilinmeyen ad olduğu gibi döner.
[[nodiscard]] QString iconAliasFor(const QString &themeName, const QString &iconName);

} // namespace ravenspot
