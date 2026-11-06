#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>
#include <array>

class QSoundEffect;

namespace ravenspot {

class SoundConfig;

class AudioEngine : public QObject {
    Q_OBJECT

public:
    // Sesleri (QSoundEffect + FFmpeg backend) önceden yükler; aksi halde ilk
    // play*() çağrısında UI thread'inde yüklenip ilk açılışı takıldırıyordu.
    void warmUp() { setupVoices(); }
    static constexpr size_t THOCK_POOL_SIZE = 4;
    static constexpr float THOCK_BASE_VOLUME = 0.28f;
    static constexpr float CREAMY_BASE_VOLUME = 0.32f;

    explicit AudioEngine(QObject *parent = nullptr);
    AudioEngine(const QString &soundsDir, const QString &configPath = QString(), QObject *parent = nullptr);
    ~AudioEngine() override;

    bool init();

    // Playback slots/invokables
    bool play(const QString &soundType);
    bool playThock();
    bool playCreamy();

    // Volume & Mute control
    int volume() const;
    bool isMuted() const;
    bool isEnabled() const;
    void setVolume(int volume);
    void setMuted(bool muted);
    void setEnabled(bool enabled);
    void toggleMute();

    size_t currentThockIndex() const noexcept { return m_thockIndex; }
    SoundConfig* config() const noexcept { return m_config.get(); }
    QString soundsDir() const { return m_soundsDir; }

    static QString locateSoundsDirectory();

Q_SIGNALS:
    void audioConfigChanged(int volume, bool muted);

private:
    void updateVoiceVolumes();
    void setupVoices();

    std::unique_ptr<SoundConfig> m_config;
    QString m_soundsDir;
    QUrl m_thockUrl;
    QUrl m_creamyUrl;

    std::array<std::unique_ptr<QSoundEffect>, THOCK_POOL_SIZE> m_thockVoices;
    size_t m_thockIndex{0};

    std::unique_ptr<QSoundEffect> m_creamyVoice;
    bool m_initialized{false};
};

} // namespace ravenspot
