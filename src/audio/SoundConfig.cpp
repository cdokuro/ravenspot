#include "SoundConfig.hpp"

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace ravenspot {

QString SoundConfig::defaultConfigFile() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!configDir.isEmpty()) {
        return configDir + QStringLiteral("/ravenspot/sound.json");
    }
    return QDir::homePath() + QStringLiteral("/.config/ravenspot/sound.json");
}

SoundConfig::SoundConfig(const QString &configFilePath, QObject *parent)
    : QObject(parent)
    , m_configFilePath(configFilePath.isEmpty() ? defaultConfigFile() : configFilePath)
{
    load();
}

void SoundConfig::setVolume(int volume) {
    const int clamped = std::clamp(volume, 0, 100);
    if (m_volume != clamped) {
        m_volume = clamped;
        save();
        Q_EMIT configChanged(m_volume, m_muted);
    }
}

void SoundConfig::setMuted(bool muted) {
    if (m_muted != muted) {
        m_muted = muted;
        save();
        Q_EMIT configChanged(m_volume, m_muted);
    }
}

void SoundConfig::setEnabled(bool enabled) {
    setMuted(!enabled);
}

void SoundConfig::toggleMute() {
    setMuted(!m_muted);
}

bool SoundConfig::load() {
    QFile file(m_configFilePath);
    if (!file.exists()) {
        m_volume = DEFAULT_VOLUME;
        m_muted = DEFAULT_MUTED;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_volume = DEFAULT_VOLUME;
        m_muted = DEFAULT_MUTED;
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        m_volume = DEFAULT_VOLUME;
        m_muted = DEFAULT_MUTED;
        return false;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("volume"))) {
        m_volume = std::clamp(obj.value(QStringLiteral("volume")).toInt(DEFAULT_VOLUME), 0, 100);
    } else {
        m_volume = DEFAULT_VOLUME;
    }

    if (obj.contains(QStringLiteral("muted"))) {
        m_muted = obj.value(QStringLiteral("muted")).toBool(DEFAULT_MUTED);
    } else if (obj.contains(QStringLiteral("enabled"))) {
        m_muted = !obj.value(QStringLiteral("enabled")).toBool(!DEFAULT_MUTED);
    } else {
        m_muted = DEFAULT_MUTED;
    }

    return true;
}

bool SoundConfig::save() const {
    const QFileInfo fi(m_configFilePath);
    const QDir dir = fi.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    QJsonObject obj;
    obj[QStringLiteral("volume")] = m_volume;
    obj[QStringLiteral("muted")] = m_muted;
    obj[QStringLiteral("enabled")] = !m_muted;

    const QJsonDocument doc(obj);

    QSaveFile file(m_configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

} // namespace ravenspot
