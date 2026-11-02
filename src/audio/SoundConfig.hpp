#pragma once

#include <QObject>
#include <QString>

namespace ravenspot {

class SoundConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY configChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY configChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY configChanged)

public:
    static constexpr int DEFAULT_VOLUME = 10;
    static constexpr bool DEFAULT_MUTED = false;

    explicit SoundConfig(const QString &configFilePath = QString(), QObject *parent = nullptr);
    ~SoundConfig() override = default;

    int volume() const noexcept { return m_volume; }
    bool isMuted() const noexcept { return m_muted; }
    bool isEnabled() const noexcept { return !m_muted; }
    QString configFilePath() const { return m_configFilePath; }

    void setVolume(int volume);
    void setMuted(bool muted);
    void setEnabled(bool enabled);
    void toggleMute();

    bool load();
    bool save() const;

    static QString defaultConfigFile();

Q_SIGNALS:
    void configChanged(int volume, bool muted);

private:
    QString m_configFilePath;
    int m_volume{DEFAULT_VOLUME};
    bool m_muted{DEFAULT_MUTED};
};

} // namespace ravenspot
