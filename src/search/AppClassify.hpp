#pragma once

#include <QString>

namespace ravenspot {

// Kullanıcının "uygulama" olarak açmayacağı girdiler: sistem ayar modülleri
// (kcm_*), kaldırıcılar, tepsi/gösterge eşlikçileri ve araç zincirlerinin
// yardımcı programları. Bunlar Apps altında "Other Apps"e toplanır.
// id: .desktop dosya adı (uzantısız), label: görünen ad, categories: Categories=
[[nodiscard]] bool isNonUserApp(const QString &id, const QString &label, const QString &categories);

} // namespace ravenspot
