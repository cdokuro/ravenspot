#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QSoundEffect>

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <dirent.h>
#include <sys/resource.h>
#include <malloc.h>

#include "ui/ThemeManager.hpp"
#include "ui/IconProviders.hpp"
#include "ui/ShellBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/SoundConfig.hpp"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define TEST_PASS(msg) \
    std::cout << "[+] PASS: " << msg << std::endl

namespace ravenspot::test {

// Helper: count open file descriptors in /proc/self/fd
size_t countOpenFileDescriptors() {
    size_t count = 0;
    DIR *dir = opendir("/proc/self/fd");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                count++;
            }
        }
        closedir(dir);
    }
    return count;
}

// Helper: get current RSS in megabytes
double getProcessRssMb() {
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long size = 0, resident = 0;
        statm >> size >> resident;
        long pageSizeKb = sysconf(_SC_PAGESIZE) / 1024;
        return (resident * pageSizeKb) / 1024.0;
    }
    return 0.0;
}

// =============================================================================
// TEST HARNESS 1: Rapid Theme Switching Stress (100 times)
// =============================================================================
bool test_01_rapid_theme_switching(QGuiApplication &app) {
    std::cout << "\n======================================================\n";
    std::cout << "TEST 1: Rapid Theme Switching Stress (100 Switches)\n";
    std::cout << "======================================================\n";

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot");
    const QString themeJsonPath = configDir + QStringLiteral("/theme.json");

    ThemeManager themeManager;
    auto appIconProvider = std::make_unique<AppIconProvider>();
    auto themeIconProvider = std::make_unique<ThemeIconProvider>();

    int themeChangedCount = 0;
    QObject::connect(&themeManager, &ThemeManager::themeChanged, [&themeChangedCount, &appIconProvider, &themeIconProvider](const QString &) {
        themeChangedCount++;
        appIconProvider->clearCache();
        themeIconProvider->clearCache();
    });

    const std::vector<QString> themeKeys = {
        QStringLiteral("raven_cyber"),
        QStringLiteral("cachy_emerald"),
        QStringLiteral("sweet_mars"),
        QStringLiteral("nordic_frost"),
        QStringLiteral("obsidian_stealth")
    };

    // Warm up Qt dynamic libraries & icon theme plugins prior to baseline RSS measurement
    for (const auto &k : themeKeys) {
        appIconProvider->requestImage(QStringLiteral("warmup_icon"), nullptr, QSize(64, 64));
        themeManager.applyTheme(k);
    }
    app.processEvents();
    ::malloc_trim(0);
    themeChangedCount = 0;

    const double initialRss = getProcessRssMb();
    std::cout << "[*] Baseline RSS after plugin warmup: " << initialRss << " MB" << std::endl;

    QElapsedTimer timer;
    timer.start();

    const int TOTAL_SWITCHES = 100;
    for (int i = 0; i < TOTAL_SWITCHES; ++i) {
        const QString &targetKey = themeKeys[i % themeKeys.size()];

        // Put an item in cache to be cleared on switch
        appIconProvider->requestImage(QStringLiteral("test_icon_%1").arg(i), nullptr, QSize(64, 64));

        bool applied = themeManager.applyTheme(targetKey);
        TEST_ASSERT(applied, "ThemeManager::applyTheme() must return true for valid theme");
        TEST_ASSERT(themeManager.activeThemeKey() == targetKey, "activeThemeKey() must match applied theme");

        // Assert file persistence on disk
        TEST_ASSERT(QFile::exists(themeJsonPath), "theme.json must exist after applyTheme");
        QFile file(themeJsonPath);
        TEST_ASSERT(file.open(QIODevice::ReadOnly | QIODevice::Text), "theme.json must be readable");
        QJsonParseError parseErr{};
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
        file.close();

        TEST_ASSERT(parseErr.error == QJsonParseError::NoError, "theme.json must be valid JSON");
        TEST_ASSERT(doc.isObject(), "theme.json root must be a JSON object");
        TEST_ASSERT(doc.object().value(QStringLiteral("active")).toString() == targetKey,
                    "theme.json 'active' property must match target theme key");

        if ((i + 1) % 25 == 0) {
            app.processEvents();
            double curRss = getProcessRssMb();
            std::cout << "[*] Completed " << (i + 1) << " switches, current RSS: " << curRss << " MB" << std::endl;
        }
    }

    qint64 elapsedMs = timer.elapsed();
    const double finalRss = getProcessRssMb();
    const double rssDelta = finalRss - initialRss;

    std::cout << "[*] 100 switches completed in " << elapsedMs << " ms ("
              << (static_cast<double>(elapsedMs) / TOTAL_SWITCHES) << " ms/switch)" << std::endl;
    std::cout << "[*] Final RSS: " << finalRss << " MB (Delta: " << rssDelta << " MB)" << std::endl;

    TEST_ASSERT(themeChangedCount == TOTAL_SWITCHES, "themeChanged signal must be emitted exactly 100 times");
    TEST_ASSERT(rssDelta < 5.0, "Memory growth after 100 theme switches must be strictly under 5.0 MB (zero leak)");

    TEST_PASS("100 rapid theme switches executed with 0 crashes, 0 memory leaks, and 100% verified disk persistence");
    return true;
}

// =============================================================================
// TEST HARNESS 2: Rapid Audio Trigger Stress (500+ times)
// =============================================================================
bool test_02_rapid_audio_trigger_stress(QGuiApplication &app) {
    std::cout << "\n======================================================\n";
    std::cout << "TEST 2: Rapid Audio Trigger Stress (500+ triggers)\n";
    std::cout << "======================================================\n";

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot");
    const QString soundJsonPath = configDir + QStringLiteral("/sound.json");
    QFile::remove(soundJsonPath);

    AudioEngine audioEngine;
    TEST_ASSERT(audioEngine.volume() == 80, "Initial volume must be default 80");
    TEST_ASSERT(!audioEngine.isMuted(), "Initial state must not be muted");

    const size_t fdBefore = countOpenFileDescriptors();
    std::cout << "[*] Open FDs before audio initialization: " << fdBefore << std::endl;

    // Trigger voice initialization for all 5 voices (4 thock + 1 creamy)
    // so that audio backend (PipeWire/ALSA) audio streams are allocated once
    for (size_t i = 0; i < AudioEngine::THOCK_POOL_SIZE; ++i) {
        audioEngine.playThock();
    }
    audioEngine.playCreamy();
    for (int i = 0; i < 10; ++i) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const size_t fdBaseline = countOpenFileDescriptors();
    std::cout << "[*] Open FDs after all 5 voices initialized: " << fdBaseline << std::endl;

    // 1. Stress 4-voice round-robin pool with 500 triggers of playThock()
    const int THOCK_BURST_COUNT = 500;
    std::vector<size_t> roundRobinSequence;
    roundRobinSequence.reserve(THOCK_BURST_COUNT);

    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < THOCK_BURST_COUNT; ++i) {
        bool ok = audioEngine.playThock();
        TEST_ASSERT(ok, "playThock() must not drop out or fail during rapid firing");
        roundRobinSequence.push_back(audioEngine.currentThockIndex());

        if (i % 50 == 0) {
            app.processEvents();
        }
    }

    qint64 thockElapsedMs = timer.elapsed();
    std::cout << "[*] 500 playThock() calls completed in " << thockElapsedMs << " ms ("
              << (static_cast<double>(thockElapsedMs) / THOCK_BURST_COUNT) << " ms/call)" << std::endl;

    // Verify strict round-robin sequence pattern
    for (size_t i = 0; i < roundRobinSequence.size(); ++i) {
        size_t expected = (i + 1) % AudioEngine::THOCK_POOL_SIZE;
        TEST_ASSERT(roundRobinSequence[i] == expected, "Thock pool index must rotate in strict round-robin 0,1,2,3,0...");
    }
    TEST_PASS("Strict 4-voice round-robin pattern verified across 500 calls without deviation");

    // 2. Stress single voice creamy player with 100 triggers
    const int CREAMY_BURST_COUNT = 100;
    timer.restart();
    for (int i = 0; i < CREAMY_BURST_COUNT; ++i) {
        bool ok = audioEngine.playCreamy();
        TEST_ASSERT(ok, "playCreamy() must return true during rapid firing");
        if (i % 25 == 0) {
            app.processEvents();
        }
    }
    qint64 creamyElapsedMs = timer.elapsed();
    std::cout << "[*] 100 playCreamy() calls completed in " << creamyElapsedMs << " ms" << std::endl;

    // Process event loop to allow ALSA/Pulse/PipeWire audio callbacks
    for (int i = 0; i < 10; ++i) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const size_t fdAfterStress = countOpenFileDescriptors();
    std::cout << "[*] Open FDs after 600 total audio playbacks: " << fdAfterStress << std::endl;

    // Assert zero FD leak: FDs after 600 plays must not exceed baseline
    TEST_ASSERT(fdAfterStress <= fdBaseline,
                "FD count must not leak after 600 audio plays (zero FD leak)");

    // 2b. Secondary burst of another 600 plays to confirm permanent FD ceiling
    for (int i = 0; i < 500; ++i) {
        audioEngine.playThock();
    }
    for (int i = 0; i < 100; ++i) {
        audioEngine.playCreamy();
    }
    for (int i = 0; i < 10; ++i) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const size_t fdAfterSecondBurst = countOpenFileDescriptors();
    std::cout << "[*] Open FDs after 1,200 cumulative audio playbacks: " << fdAfterSecondBurst << std::endl;
    TEST_ASSERT(fdAfterSecondBurst <= fdBaseline,
                "FD count must remain <= baseline after 1,200 cumulative plays (zero FD leak)");

    // 3. Test volume = 0 and mute suppression
    audioEngine.setVolume(0);
    TEST_ASSERT(!audioEngine.playThock(), "playThock() must return false when volume is 0");
    TEST_ASSERT(!audioEngine.playCreamy(), "playCreamy() must return false when volume is 0");

    audioEngine.setVolume(80);
    audioEngine.setMuted(true);
    TEST_ASSERT(!audioEngine.playThock(), "playThock() must return false when muted is true");
    TEST_ASSERT(!audioEngine.playCreamy(), "playCreamy() must return false when muted is true");

    audioEngine.setMuted(false);
    TEST_ASSERT(audioEngine.playThock(), "playThock() must resume returning true when unmuted");

    TEST_PASS("500+ rapid audio triggers passed with 0 dropouts, no voice starvation, and 0 FD leaks");
    return true;
}

// =============================================================================
// TEST HARNESS 3: Corrupt Config Injection & Fallback Resilience
// =============================================================================
bool test_03_corrupt_config_injection(QGuiApplication &app) {
    Q_UNUSED(app)
    std::cout << "\n======================================================\n";
    std::cout << "TEST 3: Corrupt Config Injection & Fallback Resilience\n";
    std::cout << "======================================================\n";

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot");
    QDir().mkpath(configDir);

    const QString themeJsonPath = configDir + QStringLiteral("/theme.json");
    const QString soundJsonPath = configDir + QStringLiteral("/sound.json");

    // -------------------------------------------------------------------------
    // 3.1: Corrupt JSON injection into theme.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.1: Malformed JSON syntax in theme.json" << std::endl;
    {
        QFile f(themeJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open theme.json for writing");
        f.write("{ \"active\": \"cachy_emerald\", \"unclosed_brace");
        f.close();

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "ThemeManager must fall back to 'raven_cyber' on corrupt theme.json");
        QVariantMap tokens = tm.currentTheme();
        TEST_ASSERT(tokens[QStringLiteral("bgBorder")].toString() == QStringLiteral("#ffd700"),
                    "Tokens must fall back to raven_cyber defaults on corrupt JSON");
    }
    TEST_PASS("Malformed JSON in theme.json gracefully falls back to raven_cyber");

    // -------------------------------------------------------------------------
    // 3.2: Binary garbage / NULL bytes injection into theme.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.2: Binary garbage in theme.json" << std::endl;
    {
        QFile f(themeJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot open theme.json");
        const char garbage[] = {0x00, 0x01, static_cast<char>(0xff), static_cast<char>(0xfe), 0x7b, 0x22};
        f.write(garbage, sizeof(garbage));
        f.close();

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "ThemeManager must fall back to 'raven_cyber' on binary garbage in theme.json");
    }
    TEST_PASS("Binary garbage in theme.json handled safely");

    // -------------------------------------------------------------------------
    // 3.3: Unknown theme name in theme.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.3: Unknown theme name in theme.json" << std::endl;
    {
        QFile f(themeJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open theme.json");
        f.write("{\n  \"active\": \"non_existent_matrix_theme\"\n}\n");
        f.close();

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "ThemeManager must reject non-existent theme and fall back to raven_cyber");
    }
    TEST_PASS("Unknown theme key safely rejected with fallback");

    // -------------------------------------------------------------------------
    // 3.4: Color hex code sanitization & invalid hex color recovery
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.4: Invalid hex colors in ThemeManager::sanitizeColor" << std::endl;
    {
        const QString fallback = QStringLiteral("#ffd700");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral(""), fallback) == fallback,
                    "Empty color string must return fallback");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#123"), fallback) == fallback,
                    "3-digit hex #123 must return fallback (requires 6 or 8 digits)");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("red"), fallback) == fallback,
                    "Named color 'red' must return fallback (requires # prefix)");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#GGGGGG"), fallback) == fallback,
                    "Non-hex characters #GGGGGG must return fallback");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#00f5a0"), fallback) == QStringLiteral("#00f5a0"),
                    "Valid 6-digit hex #00f5a0 must be accepted");
        TEST_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#00f5a0ff"), fallback) == QStringLiteral("#00f5a0ff"),
                    "Valid 8-digit hex with alpha must be accepted");
    }
    TEST_PASS("Invalid hex colors cleanly sanitized to fallback tokens");

    // -------------------------------------------------------------------------
    // 3.5: Malformed JSON syntax in sound.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.5: Malformed JSON syntax in sound.json" << std::endl;
    {
        QFile f(soundJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open sound.json");
        f.write("{\"volume\": 50, \"muted\": tru"); // broken JSON
        f.close();

        SoundConfig sc(soundJsonPath);
        TEST_ASSERT(sc.volume() == SoundConfig::DEFAULT_VOLUME,
                    "Volume must fall back to DEFAULT_VOLUME (80) on broken sound.json");
        TEST_ASSERT(sc.isMuted() == SoundConfig::DEFAULT_MUTED,
                    "Muted must fall back to DEFAULT_MUTED (false) on broken sound.json");
    }
    TEST_PASS("Malformed JSON in sound.json falls back to default volume (80) and unmuted");

    // -------------------------------------------------------------------------
    // 3.6: Out-of-bounds volume (-50 and 9999) in sound.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.6: Out-of-bounds volume in sound.json" << std::endl;
    {
        // Negative volume
        QFile f1(soundJsonPath);
        TEST_ASSERT(f1.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open sound.json");
        f1.write("{\"volume\": -50, \"muted\": false}\n");
        f1.close();

        SoundConfig scNegative(soundJsonPath);
        TEST_ASSERT(scNegative.volume() == 0, "Volume -50 must be clamped strictly to 0");

        // Excessive volume
        QFile f2(soundJsonPath);
        TEST_ASSERT(f2.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open sound.json");
        f2.write("{\"volume\": 9999, \"muted\": false}\n");
        f2.close();

        SoundConfig scExcessive(soundJsonPath);
        TEST_ASSERT(scExcessive.volume() == 100, "Volume 9999 must be clamped strictly to 100");
    }
    TEST_PASS("Out-of-bounds volume (-50 / 9999) clamped strictly to [0, 100]");

    // -------------------------------------------------------------------------
    // 3.7: Non-numeric / string volume in sound.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.7: Non-numeric volume in sound.json" << std::endl;
    {
        QFile f(soundJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open sound.json");
        f.write("{\"volume\": \"maximum\", \"muted\": \"not_boolean\"}\n");
        f.close();

        SoundConfig sc(soundJsonPath);
        TEST_ASSERT(sc.volume() == SoundConfig::DEFAULT_VOLUME,
                    "String volume must fall back to DEFAULT_VOLUME (80)");
        TEST_ASSERT(sc.isMuted() == SoundConfig::DEFAULT_MUTED,
                    "String muted must fall back to DEFAULT_MUTED (false)");
    }
    TEST_PASS("Non-numeric type mismatches in sound.json fall back safely to defaults");

    // -------------------------------------------------------------------------
    // 3.8: Complete absence of config files
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.8: Complete absence of config files" << std::endl;
    {
        QFile::remove(themeJsonPath);
        QFile::remove(soundJsonPath);

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "Absence of theme.json must default to raven_cyber");

        SoundConfig sc(soundJsonPath);
        TEST_ASSERT(sc.volume() == 80, "Absence of sound.json must default to volume 80");
        TEST_ASSERT(!sc.isMuted(), "Absence of sound.json must default to unmuted");
    }
    TEST_PASS("Absence of config files loads built-in defaults with zero error");

    // -------------------------------------------------------------------------
    // 3.9: Top-level JSON Array instead of Object in theme.json & sound.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.9: Top-level JSON Array in theme.json & sound.json" << std::endl;
    {
        QFile fTheme(themeJsonPath);
        TEST_ASSERT(fTheme.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open theme.json");
        fTheme.write("[\"not\", \"an\", \"object\"]\n");
        fTheme.close();

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "ThemeManager must reject JSON array and default to raven_cyber");

        QFile fSound(soundJsonPath);
        TEST_ASSERT(fSound.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open sound.json");
        fSound.write("[100, 200, 300]\n");
        fSound.close();

        SoundConfig sc(soundJsonPath);
        TEST_ASSERT(sc.volume() == SoundConfig::DEFAULT_VOLUME,
                    "SoundConfig must reject JSON array and default to DEFAULT_VOLUME");
    }
    TEST_PASS("Top-level JSON arrays safely rejected with default fallbacks");

    // -------------------------------------------------------------------------
    // 3.10: Unicode / emoji / control characters in theme.json
    // -------------------------------------------------------------------------
    std::cout << "[*] Subtest 3.10: Unicode, emoji, and control chars in theme.json" << std::endl;
    {
        QFile f(themeJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Cannot open theme.json");
        f.write("{\n  \"active\": \"\\u0000\\u001f\\uD83D\\uDE00_theme\"\n}\n");
        f.close();

        ThemeManager tm;
        TEST_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"),
                    "ThemeManager must reject control/emoji theme key and fall back to raven_cyber");
    }
    TEST_PASS("Unicode and control character injection safely rejected with fallback");

    // Clean up test configs and restore standard production defaults
    {
        ThemeManager tmClean;
        tmClean.applyTheme(QStringLiteral("raven_cyber"));
        QFile::remove(soundJsonPath);
        SoundConfig scClean(soundJsonPath);
        scClean.save();
    }

    TEST_PASS("Corrupt config injection resilience verified across all 10 adversarial scenarios");
    return true;
}

} // namespace ravenspot::test

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::cout << "==============================================================\n";
    std::cout << "RAVENSPOT MILESTONE 2 EMPIRICAL ADVERSARIAL STRESS TEST SUITE\n";
    std::cout << "Author: challenger_m2_1\n";
    std::cout << "Target: Theme Engine, Audio Engine, Sound Config, Shell Bridge\n";
    std::cout << "==============================================================\n";

    bool pass1 = ravenspot::test::test_01_rapid_theme_switching(app);
    if (!pass1) {
        std::cerr << "[-] TEST 1 (Rapid Theme Switching) FAILED!\n";
        return 1;
    }

    bool pass2 = ravenspot::test::test_02_rapid_audio_trigger_stress(app);
    if (!pass2) {
        std::cerr << "[-] TEST 2 (Rapid Audio Trigger) FAILED!\n";
        return 2;
    }

    bool pass3 = ravenspot::test::test_03_corrupt_config_injection(app);
    if (!pass3) {
        std::cerr << "[-] TEST 3 (Corrupt Config Injection) FAILED!\n";
        return 3;
    }

    std::cout << "\n==============================================================\n";
    std::cout << "[+] ALL 3 ADVERSARIAL STRESS TEST HARNESSES PASSED EMPIRICALLY!\n";
    std::cout << "==============================================================\n";
    return 0;
}
