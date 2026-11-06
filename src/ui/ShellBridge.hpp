#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantMap>
#include <QPointer>
#include <QTimer>
#include <QHash>

class QProcess;

class QQuickWindow;

namespace ravenspot {

class LayerShellWindow;
class ThemeManager;
class AudioEngine;
class AppIndexer;
class CommandRunner;
class CommandHistory;

class ShellBridge : public QObject {
    Q_OBJECT

    // QML Root Properties
    Q_PROPERTY(bool startHidden READ startHidden CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    // Satır içi komut çalıştırıcı (QML çıktı paneli buna bağlanır)
    Q_PROPERTY(QObject *runner READ runner CONSTANT)

public:
    explicit ShellBridge(bool startHidden = false, QObject *parent = nullptr);
    ~ShellBridge() override;

    void setLayerShellWindow(LayerShellWindow *window);
    void setThemeManager(ThemeManager *themeMgr);
    void setAudioEngine(AudioEngine *audioEngine);
    void setAppIndexer(AppIndexer *indexer);

    [[nodiscard]] bool startHidden() const noexcept { return m_startHidden; }
    [[nodiscard]] QString version() const;

signals:
    // Lifecycle and IPC Signals (connected in Menu.qml)
    void reopenRequested();
    void toggleRequested();
    void showRequested();
    void hideRequested();
    void reloadRequested();

    // UI Updates
    void themeChanged();
    void statsUpdated(int cpu, int gpu, int vol, bool muted);
    void appAudioChanged(int vol, bool muted);
    void audioConfigChanged(int volume, bool muted);
    void powerProfileChanged(const QString &profile);
    void sleepInhibitedChanged(bool inhibited);
    void telemetryUpdated(const QVariantMap &metrics);

    // Async search results (requestId ile eski yanıtlar QML'de elenir)
    void fileSearchFinished(int requestId, const QString &pattern, const QString &json);
    void windowSearchFinished(int requestId, const QString &query, const QString &json);

public slots:
    // IPC Request Forwarders
    void requestToggle() { emit toggleRequested(); }
    void requestShow() { emit showRequested(); }
    void requestHide() { emit hideRequested(); }
    void requestReload() { emit reloadRequested(); }

    // ── 1. Window & Lifecycle ──────────────────────────────────
    Q_INVOKABLE void centerWindowOnActiveScreen(QObject *qquickWindow = nullptr);
    Q_INVOKABLE void onMenuVisibilityChanged(bool visible);
    Q_INVOKABLE void toggleMenu();
    Q_INVOKABLE void showMenu();
    Q_INVOKABLE void hideMenu();
    Q_INVOKABLE void beginDrag(double initialDx = 0, double initialDy = 0);
    Q_INVOKABLE QPoint dragBy(double deltaX, double deltaY);
    Q_INVOKABLE void updateDrag(double localX = 0, double localY = 0);
    Q_INVOKABLE void endDrag();
    Q_INVOKABLE void setCardHeight(int height);
    Q_INVOKABLE bool isSwitchingScreen() const;
    Q_INVOKABLE bool perfLogging() const { return qEnvironmentVariableIsSet("RAVENSPOT_PERF"); }

    // ── 2. Audio & Volume ──────────────────────────────────────
    Q_INVOKABLE void playThock();
    Q_INVOKABLE void playCreamy();
    Q_INVOKABLE void playSound(const QString &soundType);
    Q_INVOKABLE void setAppVolume(int vol);
    Q_INVOKABLE void setAudioVolume(int vol);
    Q_INVOKABLE void toggleAppMute();
    Q_INVOKABLE void setAudioMuted(bool muted);
    Q_INVOKABLE QVariantMap getAppAudioInfo() const;
    Q_INVOKABLE QVariantMap getInitialAudioInfo() const;
    Q_INVOKABLE void setSystemVolume(int vol);
    Q_INVOKABLE void toggleSystemMute();

    // ── 3. Themes Subsystem ────────────────────────────────────
    Q_INVOKABLE QVariantMap currentTheme() const;
    Q_INVOKABLE QString listRavenThemes() const;
    Q_INVOKABLE void applyRavenTheme(const QString &key);
    Q_INVOKABLE bool applyTheme(const QString &key);
    Q_INVOKABLE QString listColorschemes() const;
    Q_INVOKABLE void applyColorscheme(const QString &name);
    Q_INVOKABLE bool toggleFavoriteTheme(const QString &name);

    // ── 4. File, Path & Config Operations ──────────────────────
    Q_INVOKABLE QString readFile(const QString &path) const;
    Q_INVOKABLE QString menuPath() const;
    Q_INVOKABLE QString userMenuPath() const;
    Q_INVOKABLE bool isDirectory(const QString &path) const;
    Q_INVOKABLE QString resolveItemPath(const QString &itemId, const QString &provider,
                                        const QString &action, const QString &value) const;
    Q_INVOKABLE QString listFolderFiles(const QString &folderName = QStringLiteral("downloads")) const;
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void openContainingFolder(const QString &path);
    Q_INVOKABLE bool moveToTrash(const QString &path);
    Q_INVOKABLE QString searchFiles(const QString &pattern) const;
    Q_INVOKABLE void requestFileSearch(const QString &pattern, int requestId);

    // ── 5. Application Launching & Execution ───────────────────
    Q_INVOKABLE QString listAppsCategory(const QString &cat) const;
    Q_INVOKABLE QString getAppCategoryCounts() const;
    Q_INVOKABLE void launchApp(const QString &desktop);
    Q_INVOKABLE QString getAppActions(const QString &desktop) const;
    Q_INVOKABLE QString searchWindows(const QString &query) const;
    Q_INVOKABLE void requestWindowSearch(const QString &query, int requestId);
    Q_INVOKABLE bool activateWindow(const QString &matchId);
    Q_INVOKABLE void editDesktopFile(const QString &desktop);
    Q_INVOKABLE void openInDiscover(const QString &desktop);
    Q_INVOKABLE void openInTerminal(const QString &path);
    Q_INVOKABLE void runInTerminal(const QString &desktopOrCmd);
    Q_INVOKABLE void runDetached(const QString &command);

    // ── Yapıştır-çalıştır komut çalıştırıcı ───────────────────
    // analyzeCommand -> {signal:int(0 none,1 weak,2 strong), dangerous, needsTerminal, firstLine, extraLines}
    Q_INVOKABLE QVariantMap analyzeCommand(const QString &text) const;
    Q_INVOKABLE void runCommandInTerminal(const QString &command);
    // false: sudo/etkileşimli olduğu için terminale yönlendirildi ya da zaten bir komut çalışıyor
    Q_INVOKABLE bool runCommandInline(const QString &command);
    Q_INVOKABLE void stopInlineCommand();
    Q_INVOKABLE QStringList commandHistory(const QString &query) const;
    [[nodiscard]] QObject *runner() const;
    Q_INVOKABLE void executeCommand(const QString &cmdStr, bool inTerminal = false);
    Q_INVOKABLE QString bash(const QString &script);
    Q_INVOKABLE bool isCommandAvailable(const QString &command) const;
    Q_INVOKABLE QString detectGpuVendor() const;
    Q_INVOKABLE QString getNashiPath() const;
    Q_INVOKABLE void installPackage(const QString &packageName, const QString &customCommand);
    Q_INVOKABLE void installNashi();

    // ── 6. Power & System Actions ──────────────────────────────
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void rebootSystem();
    Q_INVOKABLE void lockScreen();
    Q_INVOKABLE void suspendSystem();
    Q_INVOKABLE void logoutUser();
    Q_INVOKABLE void saveSession();
    Q_INVOKABLE void restartPlasma();
    Q_INVOKABLE QString listPowerProfiles() const;
    Q_INVOKABLE void applyPowerProfile(const QString &profile);
    Q_INVOKABLE QString getPowerProfile() const;
    Q_INVOKABLE bool setPowerProfile(const QString &profile);
    Q_INVOKABLE bool isSleepInhibited() const;
    Q_INVOKABLE bool setSleepInhibited(bool inhibit);

    // ── 7. Clipboard & Process Management ──────────────────────
    Q_INVOKABLE QString listClipboard() const;
    Q_INVOKABLE void setClipboard(const QString &text);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE QString listProcesses(const QString &query = QString()) const;
    Q_INVOKABLE bool killProcess(const QString &pid);

    // ── 8. Telemetry & Updates ─────────────────────────────────
    Q_INVOKABLE QVariantMap getSysMonitorInfo() const;
    Q_INVOKABLE QVariantMap getUpdateInfo() const;
    Q_INVOKABLE void launchUpdate();
    Q_INVOKABLE void openSystemMonitor(const QString &section = QStringLiteral("overview"));

private:
    void updateTelemetry();
    void activateDolphinWindow(const QString &folderHint);

    QProcess *m_fileSearchProc{nullptr}; // devam eden async fd araması (yeni sorgu gelince iptal)
    CommandRunner *m_commandRunner{nullptr};
    CommandHistory *m_commandHistory{nullptr};
    mutable QHash<QString, bool> m_execCache; // PATH araması önbelleği (menü kapanınca temizlenir)

    bool m_startHidden{false};
    QPointer<LayerShellWindow> m_layerShellWindow;
    ThemeManager *m_themeManager{nullptr};
    AudioEngine *m_audioEngine{nullptr};
    AppIndexer *m_appIndexer{nullptr};

    uint m_sleepInhibitCookie{0};
    bool m_sleepInhibited{false};
    QString m_powerProfile{QStringLiteral("balanced")};
    QTimer m_telemetryTimer;

    // CPU measurement state
    unsigned long long m_lastTotalUser{0};
    unsigned long long m_lastTotalUserLow{0};
    unsigned long long m_lastTotalSys{0};
    unsigned long long m_lastTotalIdle{0};
    int m_lastCpuPercent{0};
    int m_lastGpuPercent{0};
};

} // namespace ravenspot
