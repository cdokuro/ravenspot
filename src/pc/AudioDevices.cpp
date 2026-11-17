#include "AudioDevices.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

namespace ravenspot::pc {

QVariantList parseAudioDevices(const QByteArray &pactlJson, const QString &defaultName) {
    QVariantList out;
    const QJsonDocument doc = QJsonDocument::fromJson(pactlJson);
    if (!doc.isArray()) return out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("name")).toString();
        if (id.isEmpty()) continue;
        const QString deviceClass = o.value(QStringLiteral("properties")).toObject()
                                        .value(QStringLiteral("device.class")).toString();
        if (deviceClass == QStringLiteral("monitor") || id.endsWith(QStringLiteral(".monitor"))) continue;
        const QString desc = o.value(QStringLiteral("description")).toString();
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("name"), desc.isEmpty() ? id : desc);
        m.insert(QStringLiteral("isDefault"), id == defaultName);
        out.append(m);
    }
    return out;
}

} // namespace ravenspot::pc
