#include "AudioEngine.hpp"
#include "SoundConfig.hpp"

#include <QSoundEffect>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace ravenspot {

QString AudioEngine::locateSoundsDirectory() {
    // 1. Environment variable override
    const QByteArray envDir = qgetenv("RAVENSPOT_SOUNDS_DIR");
    if (!envDir.isEmpty()) {
        const QString p = QString::fromUtf8(envDir);
        if (QDir(p).exists()) return p;
    }

    // 2. Relative to application executable binary
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QDir(appDir + QStringLiteral("/sounds")).exists()) {
        return appDir + QStringLiteral("/sounds");
    }

    // 3. Source repository / current working directory
    const QString cwdSounds = QDir::currentPath() + QStringLiteral("/sounds");
    if (QDir(cwdSounds).exists()) {
        return cwdSounds;
    }

    // 4. Reference ravenspot sounds path (development fallback)
    const QString refSounds = QStringLiteral("/home/dokuro/Masaüstü/ravenspot/sounds");
    if (QDir(refSounds).exists()) {
        return refSounds;
    }

    // 5. Standard XDG data locations (/usr/share/ravenspot/sounds, etc.)
    const QString xdgPath = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("ravenspot/sounds"),
        QStandardPaths::LocateDirectory
    );
    if (!xdgPath.isEmpty()) {
        return xdgPath;
    }

    return QString();
}

AudioEngine::AudioEngine(QObject *parent)
    : AudioEngine(QString(), QString(), parent)
{
}

AudioEngine::AudioEngine(const QString &soundsDir, const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_config(std::make_unique<SoundConfig>(configPath, this))
    , m_soundsDir(soundsDir.isEmpty() ? locateSoundsDirectory() : soundsDir)
{
    init();
}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::init() {
    if (m_initialized) return true;

    connect(m_config.get(), &SoundConfig::configChanged, this, [this](int volume, bool muted) {
        updateVoiceVolumes();
        Q_EMIT audioConfigChanged(volume, muted);
    });

    m_initialized = true;
    return true;
}

void AudioEngine::setupVoices() {
    if (!m_thockVoices[0] && !m_soundsDir.isEmpty()) {
        const QString thockPath = m_soundsDir + QStringLiteral("/thock.wav");
        const QString creamyPath = m_soundsDir + QStringLiteral("/creamy.wav");

        if (QFileInfo::exists(thockPath)) {
            m_thockUrl = QUrl::fromLocalFile(thockPath);
            for (size_t i = 0; i < THOCK_POOL_SIZE; ++i) {
                auto voice = std::make_unique<QSoundEffect>(this);
                voice->setSource(m_thockUrl);
                voice->setLoopCount(1);
                m_thockVoices[i] = std::move(voice);
            }
        }

        if (QFileInfo::exists(creamyPath)) {
            m_creamyUrl = QUrl::fromLocalFile(creamyPath);
            m_creamyVoice = std::make_unique<QSoundEffect>(this);
            m_creamyVoice->setSource(m_creamyUrl);
            m_creamyVoice->setLoopCount(1);
        }
        updateVoiceVolumes();
    }
}

void AudioEngine::updateVoiceVolumes() {
    const float effVol = m_config->isMuted() ? 0.0f : (static_cast<float>(m_config->volume()) / 100.0f);
    const float thockVol = std::clamp(THOCK_BASE_VOLUME * effVol, 0.0f, 1.0f);
    const float creamyVol = std::clamp(CREAMY_BASE_VOLUME * effVol, 0.0f, 1.0f);

    for (auto &voice : m_thockVoices) {
        if (voice) {
            voice->setVolume(thockVol);
            voice->setMuted(m_config->isMuted());
        }
    }

    if (m_creamyVoice) {
        m_creamyVoice->setVolume(creamyVol);
        m_creamyVoice->setMuted(m_config->isMuted());
    }
}

bool AudioEngine::play(const QString &soundType) {
    if (soundType == QLatin1String("thock")) {
        return playThock();
    } else if (soundType == QLatin1String("creamy")) {
        return playCreamy();
    }
    return false;
}

bool AudioEngine::playThock() {
    if (m_config->isMuted() || m_config->volume() <= 0) {
        return false;
    }

    setupVoices();

    if (m_thockVoices[m_thockIndex]) {
        auto &voice = m_thockVoices[m_thockIndex];
        m_thockIndex = (m_thockIndex + 1) % THOCK_POOL_SIZE;
        if (voice->status() != QSoundEffect::Error) {
            voice->play();
        }
        return true;
    }
    return false;
}

bool AudioEngine::playCreamy() {
    if (m_config->isMuted() || m_config->volume() <= 0) {
        return false;
    }

    setupVoices();

    if (m_creamyVoice) {
        if (m_creamyVoice->status() != QSoundEffect::Error) {
            m_creamyVoice->play();
        }
        return true;
    }
    return false;
}

int AudioEngine::volume() const {
    return m_config->volume();
}

bool AudioEngine::isMuted() const {
    return m_config->isMuted();
}

bool AudioEngine::isEnabled() const {
    return m_config->isEnabled();
}

void AudioEngine::setVolume(int volume) {
    m_config->setVolume(volume);
}

void AudioEngine::setMuted(bool muted) {
    m_config->setMuted(muted);
}

void AudioEngine::setEnabled(bool enabled) {
    m_config->setEnabled(enabled);
}

void AudioEngine::toggleMute() {
    m_config->toggleMute();
}

} // namespace ravenspot
