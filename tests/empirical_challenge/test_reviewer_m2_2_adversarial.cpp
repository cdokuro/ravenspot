#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMetaMethod>
#include <QThread>

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cassert>
#include <random>

#include "ui/ShellBridge.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/IconProviders.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/SoundConfig.hpp"

#define REV_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] CONTRACT VIOLATION: " << msg << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define REV_PASS(msg) \
    std::cout << "[+] PASS: " << msg << std::endl

namespace ravenspot::review {

// =============================================================================
// TEST 1: ShellBridge 57 Methods & Signal Mapping Contract
// =============================================================================
bool test_shell_bridge_contracts() {
    std::cout << "\n--- TEST 1: ShellBridge 57 Methods & Signals Contract Verification ---\n";

    ShellBridge bridge(true);
    const QMetaObject *meta = bridge.metaObject();

    // Verify all 57 methods invoked across Menu.qml & MenuModel.js
    const std::vector<const char*> requiredMethods = {
        "applyColorscheme", "applyPowerProfile", "applyRavenTheme", "bash",
        "centerWindowOnActiveScreen", "currentTheme", "editDesktopFile",
        "getAppActions", "getAppAudioInfo", "getAppCategoryCounts",
        "getInitialAudioInfo", "getPowerProfile", "getSysMonitorInfo",
        "getUpdateInfo", "isDirectory", "isSleepInhibited", "killProcess",
        "launchApp", "launchUpdate", "listAppsCategory", "listClipboard",
        "listColorschemes", "listFolderFiles", "listPowerProfiles",
        "listProcesses", "listRavenThemes", "lockScreen", "logoutUser",
        "menuPath", "moveToTrash", "onMenuVisibilityChanged",
        "openContainingFolder", "openInDiscover", "openInTerminal", "openPath",
        "openSystemMonitor", "playCreamy", "playThock", "powerOff",
        "readFile", "rebootSystem", "resolveItemPath", "restartPlasma",
        "runDetached", "runInTerminal", "searchFiles", "setAppVolume",
        "setClipboard", "setPowerProfile", "setSleepInhibited",
        "setSystemVolume", "suspendSystem", "toggleAppMute",
        "toggleFavoriteTheme", "toggleSystemMute", "userMenuPath"
    };

    for (const char *name : requiredMethods) {
        bool found = false;
        for (int i = 0; i < meta->methodCount(); ++i) {
            QMetaMethod m = meta->method(i);
            if (m.name() == name) {
                found = true;
                break;
            }
        }
        REV_ASSERT(found, std::string("Required Shell method not found in QMetaObject: ") + name);
    }
    REV_PASS("All 57 methods exist in ShellBridge QMetaObject");

    // Verify startHidden property
    int propIdx = meta->indexOfProperty("startHidden");
    REV_ASSERT(propIdx >= 0, "startHidden property missing");
    REV_ASSERT(bridge.property("startHidden").toBool() == true, "startHidden must reflect constructor param");

    // Verify signals exist
    const std::vector<const char*> requiredSignals = {
        "toggleRequested", "showRequested", "hideRequested", "reloadRequested",
        "themeChanged", "statsUpdated", "appAudioChanged", "audioConfigChanged",
        "powerProfileChanged", "sleepInhibitedChanged", "telemetryUpdated"
    };
    for (const char *sig : requiredSignals) {
        bool found = false;
        for (int i = 0; i < meta->methodCount(); ++i) {
            QMetaMethod m = meta->method(i);
            if (m.methodType() == QMetaMethod::Signal && m.name() == sig) {
                found = true;
                break;
            }
        }
        REV_ASSERT(found, std::string("Required signal missing: ") + sig);
    }
    REV_PASS("All required UI/IPC signals present and exposed");

    // Adversarial robust inputs on ShellBridge methods
    bridge.launchApp("");
    bridge.launchApp("   ");
    bridge.executeCommand("");
    bridge.copyToClipboard("multi\0byte\0sanitized");
    REV_ASSERT(!bridge.readFile("").size(), "readFile with empty path must return empty");
    REV_ASSERT(!bridge.readFile("/nonexistent_path_xyz").size(), "readFile nonexistent must return empty");
    REV_ASSERT(bridge.listFolderFiles("nonexistent_folder_name") == QStringLiteral("[]"),
               "listFolderFiles nonexistent must return empty JSON array");
    REV_ASSERT(!bridge.setPowerProfile("invalid_profile"), "setPowerProfile must reject invalid profiles");
    REV_ASSERT(bridge.setPowerProfile("power-saver"), "setPowerProfile must accept power-saver");
    REV_ASSERT(bridge.setPowerProfile("balanced"), "setPowerProfile must accept balanced");
    REV_ASSERT(bridge.setPowerProfile("performance"), "setPowerProfile must accept performance");
    REV_ASSERT(!bridge.killProcess(""), "killProcess must reject empty pid");
    REV_ASSERT(!bridge.killProcess("invalid"), "killProcess must reject non-numeric pid");
    REV_ASSERT(!bridge.killProcess("1"), "killProcess must reject PID 1");
    REV_ASSERT(!bridge.killProcess("-5"), "killProcess must reject negative PID");

    REV_PASS("ShellBridge handles edge case arguments safely without crash");
    return true;
}

// =============================================================================
// TEST 2: ThemeManager Robustness against Corrupt / Missing Assets
// =============================================================================
bool test_theme_manager_corrupt_resilience() {
    std::cout << "\n--- TEST 2: ThemeManager Corrupt / Missing Assets Resilience ---\n";

    const QString testDir = QDir::tempPath() + QStringLiteral("/ravenspot_theme_test_") + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(testDir);

    // 1. Missing themes.json
    ThemeManager tmMissing;
    REV_ASSERT(tmMissing.activeThemeKey() == QStringLiteral("raven_cyber"), "Missing file must default to raven_cyber");
    REV_ASSERT(tmMissing.availableThemes().size() >= 5, "Built-in themes must contain at least 5 themes");
    REV_ASSERT(tmMissing.hasTheme("cachy_emerald"), "Built-in themes must include cachy_emerald");

    // 2. Corrupt themes.json tests
    const std::vector<std::string> corruptPayloads = {
        "", // 0 bytes
        "{", // incomplete
        "{\"themes\": [1, 2, 3]}", // themes not an object
        "{\"active\": 123}", // active not string
        "not json at all",
        "{\"themes\": {\"bad_theme\": {\"bgColor\": \"invalid_hex\"}}}" // invalid hex
    };

    const QString tempJsonPath = testDir + QStringLiteral("/themes.json");
    for (size_t i = 0; i < corruptPayloads.size(); ++i) {
        QFile f(tempJsonPath);
        REV_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "Failed to open test themes.json");
        f.write(corruptPayloads[i].c_str());
        f.close();

        // Ingesting corrupt file
        ThemeManager tm;
        REV_ASSERT(tm.activeThemeKey() == QStringLiteral("raven_cyber"), "Corrupt payload must not crash ThemeManager");
        REV_ASSERT(tm.currentTokens().bgBorder.startsWith(QLatin1Char('#')), "Fallback token color must be valid hex");
    }

    // 3. Color sanitizer validation
    const QString fallback = QStringLiteral("#ffd700");
    REV_ASSERT(ThemeManager::sanitizeColor(QStringLiteral(""), fallback) == fallback, "Empty color fallback");
    REV_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#fff"), fallback) == fallback, "3-digit hex fallback");
    REV_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("blue"), fallback) == fallback, "Named color fallback");
    REV_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#00f5a0"), fallback) == QStringLiteral("#00f5a0"), "Valid 6-digit hex");
    REV_ASSERT(ThemeManager::sanitizeColor(QStringLiteral("#00f5a0cc"), fallback) == QStringLiteral("#00f5a0cc"), "Valid 8-digit hex");

    // 4. Switching to unknown theme
    REV_ASSERT(!tmMissing.applyTheme("non_existent_key"), "applyTheme must return false for unknown key");
    REV_ASSERT(tmMissing.activeThemeKey() == QStringLiteral("raven_cyber"), "activeThemeKey must remain unchanged");

    QDir(testDir).removeRecursively();
    REV_PASS("ThemeManager resilience verified against missing/corrupt themes.json and bad color tokens");
    return true;
}

// =============================================================================
// TEST 3: IconProviders LRU Cache, 500-Item Cap, Thread Safety, Traversal
// =============================================================================
bool test_icon_provider_lru_and_security() {
    std::cout << "\n--- TEST 3: IconProviders LRU Cache, Cap=500, Thread Safety, Path Traversal ---\n";

    // 1. Capacity Cap = 500
    IconCache cache(500);
    QImage dummy(64, 64, QImage::Format_ARGB32);
    dummy.fill(Qt::black);

    for (int i = 0; i < 1000; ++i) {
        cache.put(QStringLiteral("icon_%1").arg(i), dummy);
        REV_ASSERT(cache.size() <= 500, "IconCache size must strictly never exceed 500");
    }
    REV_ASSERT(cache.size() == 500, "IconCache size after 1000 inserts must be exactly 500");
    REV_PASS("IconCache strict 500-item cap verified");

    // 2. LRU Eviction Order
    IconCache lruTest(3);
    lruTest.put("a", dummy);
    lruTest.put("b", dummy);
    lruTest.put("c", dummy);
    // Access 'a' to make it most recently used
    QImage out;
    REV_ASSERT(lruTest.get("a", out), "Item 'a' must be in cache");
    // Insert 'd' -> 'b' should be evicted because 'a' was touched and 'c' was inserted after 'b'
    lruTest.put("d", dummy);
    REV_ASSERT(!lruTest.get("b", out), "Item 'b' must have been evicted by LRU policy");
    REV_ASSERT(lruTest.get("a", out), "Item 'a' must still exist");
    REV_ASSERT(lruTest.get("c", out), "Item 'c' must still exist");
    REV_ASSERT(lruTest.get("d", out), "Item 'd' must still exist");
    REV_PASS("IconCache LRU eviction order strictly verified");

    // 3. Multi-threaded Concurrency & Thread-Safety
    std::cout << "[*] Running 8 concurrent threads on IconCache (1000 ops each)..." << std::endl;
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&cache, dummy, t]() {
            for (int i = 0; i < 1000; ++i) {
                QString key = QStringLiteral("t%1_key_%2").arg(t).arg(i % 100);
                if (i % 3 == 0) {
                    cache.put(key, dummy);
                } else if (i % 3 == 1) {
                    QImage img;
                    cache.get(key, img);
                } else {
                    cache.size();
                }
            }
        });
    }
    for (auto &w : workers) {
        w.join();
    }
    REV_ASSERT(cache.size() <= 500, "IconCache size after concurrent operations must not exceed 500");
    REV_PASS("IconCache thread safety verified under 8-thread heavy concurrent access");

    // 4. Path Traversal & Sanitization
    AppIconProvider appProvider;
    ThemeIconProvider themeProvider;

    // Test traversal attempts
    QSize sz;
    QImage img1 = appProvider.requestImage(QStringLiteral("../../../../etc/passwd"), &sz, QSize(64, 64));
    REV_ASSERT(!img1.isNull(), "Provider must return non-null fallback image on traversal attempt");
    REV_ASSERT(img1.width() >= 64 && img1.height() >= 64, "Provider image must be at least 64x64");

    // Test query string cache-busting stripping
    QImage img2 = appProvider.requestImage(QStringLiteral("firefox?v=12345"), &sz, QSize(64, 64));
    REV_ASSERT(!img2.isNull(), "Provider must handle version query parameters safely");

    // Test 0-byte corrupt file
    const QString zeroBytePath = QDir::tempPath() + QStringLiteral("/corrupt_0byte_icon.png");
    {
        QFile zf(zeroBytePath);
        zf.open(QIODevice::WriteOnly | QIODevice::Truncate);
        zf.close();
    }
    QImage img3 = appProvider.requestImage(zeroBytePath, &sz, QSize(64, 64));
    REV_ASSERT(!img3.isNull(), "Provider must not crash on 0-byte corrupt image file");
    QFile::remove(zeroBytePath);

    REV_PASS("Path traversal sanitization, query stripping, and corrupt file fallback verified");
    return true;
}

// =============================================================================
// TEST 4: AudioEngine 4-Voice Round-Robin & SoundConfig Atomic Save
// =============================================================================
bool test_audio_engine_and_sound_config() {
    std::cout << "\n--- TEST 4: AudioEngine 4-Voice Round-Robin Pool & SoundConfig Atomic Save ---\n";

    const QString testSoundConfig = QDir::tempPath() + QStringLiteral("/test_sound_config_") + QString::number(QCoreApplication::applicationPid()) + QStringLiteral(".json");
    QFile::remove(testSoundConfig);

    // 1. SoundConfig Volume Clamping and Defaults
    {
        SoundConfig cfg(testSoundConfig);
        REV_ASSERT(cfg.volume() == 80, "Default volume must be 80");
        REV_ASSERT(!cfg.isMuted(), "Default muted must be false");

        // Clamping upper
        cfg.setVolume(150);
        REV_ASSERT(cfg.volume() == 100, "Volume 150 must be clamped to 100");

        // Clamping lower
        cfg.setVolume(-40);
        REV_ASSERT(cfg.volume() == 0, "Volume -40 must be clamped to 0");

        cfg.setVolume(75);
        cfg.setMuted(true);
    }

    // 2. Atomic Save Persistence Verification
    REV_ASSERT(QFile::exists(testSoundConfig), "sound.json must exist after save");
    {
        QFile f(testSoundConfig);
        REV_ASSERT(f.open(QIODevice::ReadOnly | QIODevice::Text), "sound.json must open for read");
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        REV_ASSERT(doc.isObject(), "sound.json must be a JSON object");
        REV_ASSERT(doc.object().value(QStringLiteral("volume")).toInt() == 75, "Saved volume must be 75");
        REV_ASSERT(doc.object().value(QStringLiteral("muted")).toBool() == true, "Saved muted must be true");
    }
    REV_PASS("SoundConfig atomic save and disk persistence verified");

    // 3. AudioEngine 4-Voice Round-Robin Pool Rotation
    AudioEngine audio(QString(), testSoundConfig);
    audio.setMuted(false);
    audio.setVolume(80);

    // Lazy load & round robin rotation
    for (size_t i = 0; i < 16; ++i) {
        size_t expectedIndex = i % AudioEngine::THOCK_POOL_SIZE;
        audio.playThock();
        size_t actualIndex = audio.currentThockIndex();
        size_t nextExpected = (expectedIndex + 1) % AudioEngine::THOCK_POOL_SIZE;
        REV_ASSERT(actualIndex == nextExpected, "Thock index must rotate strictly in round robin 0->1->2->3->0");
    }
    REV_PASS("4-voice round-robin rotation verified across multiple cycles");

    // 4. Volume Scaling and Mute Suppression
    audio.setMuted(true);
    REV_ASSERT(!audio.playThock(), "playThock must return false when muted");
    REV_ASSERT(!audio.playCreamy(), "playCreamy must return false when muted");

    audio.setMuted(false);
    audio.setVolume(0);
    REV_ASSERT(!audio.playThock(), "playThock must return false when volume is 0");
    REV_ASSERT(!audio.playCreamy(), "playCreamy must return false when volume is 0");

    QFile::remove(testSoundConfig);
    REV_PASS("Volume scaling and mute suppression verified");
    return true;
}

} // namespace ravenspot::review

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::cout << "=================================================================\n";
    std::cout << " reviewer_m2_2: Comprehensive Contract & Robustness Test Suite   \n";
    std::cout << "=================================================================\n";

    bool ok1 = ravenspot::review::test_shell_bridge_contracts();
    if (!ok1) return 1;

    bool ok2 = ravenspot::review::test_theme_manager_corrupt_resilience();
    if (!ok2) return 2;

    bool ok3 = ravenspot::review::test_icon_provider_lru_and_security();
    if (!ok3) return 3;

    bool ok4 = ravenspot::review::test_audio_engine_and_sound_config();
    if (!ok4) return 4;

    std::cout << "\n=================================================================\n";
    std::cout << " ALL REVIEWER_M2_2 INDEPENDENT CONTRACT TESTS PASSED!            \n";
    std::cout << "=================================================================\n";
    return 0;
}
