#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

namespace ravenspot::pc {

// `pactl --format=json list sinks|sources` çıktısını seçilebilir cihazlara
// çevirir: [{id, name, isDefault}]. Monitör kaynakları (çıkışın kaydı) elenir;
// name, description (yoksa düğüm adı); id, pactl'a geri verilecek düğüm adı.
[[nodiscard]] QVariantList parseAudioDevices(const QByteArray &pactlJson, const QString &defaultName);

} // namespace ravenspot::pc
