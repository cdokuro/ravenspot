#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QPointer>
#include <QTimer>

class QQuickWindow;

namespace ravenspot {

class LayerShellWindow;
class ThemeManager;
class AudioEngine;
class AppIndexer;

class ShellBridge : public QObject {
    Q_OBJECT

    // QML Root Properties
    Q_PROPERTY(bool startHidden READ startHidden CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

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

    // ── 5. Application Launching & Execution ───────────────────
    Q_INVOKABLE QString listAppsCategory(const QString &cat) const;
    Q_INVOKABLE QString getAppCategoryCounts() const;
    Q_INVOKABLE void launchApp(const QString &desktop);
    Q_INVOKABLE QString getAppActions(const QString &desktop) const;
    Q_INVOKABLE void editDesktopFile(const QString &desktop);
    Q_INVOKABLE void openInDiscover(const QString &desktop);
    Q_INVOKABLE void openInTerminal(const QString &path);
    Q_INVOKABLE void runInTerminal(const QString &desktopOrCmd);
    Q_INVOKABLE void runDetached(const QString &command);
    Q_INVOKABLE void executeCommand(const QString &cmdStr, bool inTerminal = false);
    Q_INVOKABLE QString bash(const QString &script);

    // ── 6. Power & System Actions ──────────────────────────────
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void rebootSystem();
    Q_INVOKABLE void lockScreen();
    Q_INVOKABLE void suspendSystem();
    Q_INVOKABLE void logoutUser();
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

    bool m_startHidden{false};
    QPointer<LayerShellWindow> m_layerShellWindow;
    ThemeManager *m_themeManager{nullptr};
    AudioEngine *m_audioEngine{nullptr};
    AppIndexer *m_appIndexer{nullptr};

    uint m_sleepInhibitCookie{0};
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
