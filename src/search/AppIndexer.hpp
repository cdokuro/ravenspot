#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ravenspot {

struct DesktopAction {
    QString id;       // e.g. "new-window"
    QString name;     // e.g. "New Window" or "Yeni Pencere"
    QString exec;     // e.g. "firefox --new-window" (cleaned)
    QString icon;     // optional action icon
};

struct AppEntry {
    QString id;             // filename without ".desktop", e.g. "org.kde.dolphin"
    QString label;          // localized display name (Turkish prioritized, fallback EN)
    QString labelEn;        // raw English Name
    QString labelTr;        // localized Turkish Name
    QString genericName;    // GenericName / GenericName[tr]
    QString comment;        // Comment / Comment[tr]
    QString icon;           // Icon key (theme name or absolute path)
    QString desktopPath;    // Absolute path: "/usr/share/applications/firefox.desktop"
    QString rawExec;        // Original Exec string from file
    QString cleanedExec;    // Cleaned Exec with %u, %U, %f, etc. removed
    QString category;       // Canonical category: "games", "internet", "development",
                            // "multimedia", "graphics", "office", "system", "utilities"
    QStringList rawCategories; // Raw Categories= from desktop entry
    QStringList aliases;    // All alias terms for search matching
    QStringList searchTerms;// Tokenized lowercase terms
    std::vector<DesktopAction> actions; // Sub-actions from [Desktop Action ...]
    bool terminal{false};   // Terminal=true
    bool noDisplay{false};  // NoDisplay=true
    bool hidden{false};     // Hidden=true
    bool isKcm{false};      // kcm_*.desktop exception
    QString workingDir;     // Path= working directory
};

struct AppSearchResult {
    const AppEntry *app{nullptr};
    int score{0};
};

// Immutable snapshot for lock-free read access
struct AppIndexSnapshot {
    std::vector<AppEntry> allApps;
    std::unordered_map<QString, size_t> idLookup;
    std::unordered_map<QString, size_t> desktopPathLookup;
    std::unordered_map<QString, std::vector<size_t>> categoryIndices;
    std::unordered_map<QString, QString> cachedCategoryJson;
    QString cachedCategoryCountsJson;
};

class AppIndexer : public QObject {
    Q_OBJECT

public:
    explicit AppIndexer(QObject *parent = nullptr);
    ~AppIndexer() override;

    // Initialization & Lifecycle
    void initialize();
    void rescanNow();

    // QML Bridge API (used by ShellBridge)
    [[nodiscard]] QString listAppsCategoryJson(const QString &cat) const;
    [[nodiscard]] QString getAppCategoryCountsJson() const;
    [[nodiscard]] QString getAppActionsJson(const QString &desktopPath) const;
    void launchApp(const QString &desktopOrExec);

    // Context Action Helpers
    void openContainingFolder(const QString &desktopPath) const;
    void editDesktopFile(const QString &desktopPath) const;
    void openInDiscover(const QString &desktopPath) const;
    void runInTerminal(const QString &desktopOrCmd) const;

    // Icon Resolution Integration
    [[nodiscard]] QString resolveAppIcon(const QString &appId) const;

    // Search Engine Integration (M3)
    [[nodiscard]] std::vector<AppSearchResult> searchApps(const QString &query,
                                                          const QString &category = QStringLiteral("all"),
                                                          size_t maxResults = 50) const;

    // Static Utilities (exposed for testability)
    static QString cleanExecCommand(const QString &rawExec);
    static QString categorizeApp(const QString &categoriesStr);
    static QStringList standardApplicationDirectories();
    static bool parseDesktopFile(const QString &filePath, AppEntry &outEntry, const QString &preferredLang = QStringLiteral("tr"));

signals:
    void indexUpdated();

private slots:
    void onDirectoryChanged(const QString &path);
    void onDebounceTimeout();

private:
    void rebuildIndex();
    static void scanDirectoryRecursive(const QString &dirPath,
                                       std::unordered_map<QString, QString> &seenBasenames,
                                       std::unordered_set<uint64_t> &visitedInodes);

    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    std::atomic<std::shared_ptr<const AppIndexSnapshot>> m_snapshot;
};

} // namespace ravenspot
