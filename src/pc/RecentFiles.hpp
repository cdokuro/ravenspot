#pragma once

#include <QString>
#include <QVariantList>

namespace ravenspot::pc {

// ~/.local/share/recently-used.xbel: en yeni önce, yalnızca hâlâ var olan yerel
// dosyalar, ada göre büyük/küçük harf duyarsız filtre. [{path, name, isDir, modified}]
QVariantList recentFiles(const QString &xbelPath, const QString &query, int limit = 20);
QString defaultXbelPath();

} // namespace ravenspot::pc
