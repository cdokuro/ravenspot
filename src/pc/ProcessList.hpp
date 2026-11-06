#pragma once

#include <QString>
#include <QVariantList>

namespace ravenspot::pc {

// /proc altından kullanıcının KENDİ süreçlerini listeler (PID 1 ve diğer
// kullanıcılar hariç), ad veya komut satırında büyük/küçük harf duyarsız eşleşme,
// RSS'e göre azalan. Dönüş: [{pid, name, cmd, rssKb, rssText}]
QVariantList listProcesses(const QString &query, const QString &procRoot, uint uid, int limit = 15);

} // namespace ravenspot::pc
