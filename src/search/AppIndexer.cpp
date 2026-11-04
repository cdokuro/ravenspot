#include "AppIndexer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusArgument>
#include <QUrl>
#include <QDBusReply>
#include <QDBusInterface>
#include <algorithm>
#include <sys/stat.h>

namespace ravenspot {

AppIndexer::AppIndexer(QObject *parent)
    : QObject(parent),
      m_snapshot(std::make_shared<const AppIndexSnapshot>())
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(500);

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &AppIndexer::onDirectoryChanged);
    connect(&m_debounceTimer, &QTimer::timeout, this, &AppIndexer::onDebounceTimeout);
}

AppIndexer::~AppIndexer() = default;

void AppIndexer::initialize() {
    rebuildIndex();

    // Watch standard directories for live updates
    const auto dirs = standardApplicationDirectories();
    for (const auto &d : dirs) {
        if (QDir(d).exists() && !m_watcher.directories().contains(d)) {
            m_watcher.addPath(d);
        }
    }
}

void AppIndexer::rescanNow() {
    rebuildIndex();
}

void AppIndexer::onDirectoryChanged(const QString &path) {
    Q_UNUSED(path)
    // Restart debounce timer to prevent indexing thrash during package updates
    m_debounceTimer.start();
}

void AppIndexer::onDebounceTimeout() {
    rebuildIndex();
}

QStringList AppIndexer::standardApplicationDirectories() {
    QStringList dirs;
    const QString home = QDir::homePath();

    // 1. User local applications (highest precedence)
    dirs.append(home + QStringLiteral("/.local/share/applications"));

    // 2. User Flatpak exports
    dirs.append(home + QStringLiteral("/.local/share/flatpak/exports/share/applications"));

    // 3. System Flatpak exports
    dirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));

    // 4. System Snap applications
    dirs.append(QStringLiteral("/var/lib/snapd/desktop/applications"));

    // 5. XDG_DATA_DIRS application paths
    const QString xdgDataDirs = QString::fromUtf8(qgetenv("XDG_DATA_DIRS"));
    const QStringList splitDirs = (xdgDataDirs.isEmpty()
        ? QStringLiteral("/usr/local/share:/usr/share")
        : xdgDataDirs).split(u':', Qt::SkipEmptyParts);

    for (const auto &d : splitDirs) {
        dirs.append(d + QStringLiteral("/applications"));
    }

    return dirs;
}

void AppIndexer::scanDirectoryRecursive(const QString &dirPath,
                                        std::unordered_map<QString, QString> &seenBasenames,
                                        std::unordered_set<uint64_t> &visitedInodes) {
    QDir dir(dirPath);
    if (!dir.exists()) return;

    // Inode composite protection against circular symlinks
    struct stat st;
    if (::stat(dirPath.toUtf8().constData(), &st) == 0) {
        const uint64_t inodeKey = (static_cast<uint64_t>(st.st_dev) << 32) | static_cast<uint64_t>(st.st_ino);
        if (visitedInodes.contains(inodeKey)) return;
        visitedInodes.insert(inodeKey);
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &fi : entries) {
        if (fi.isDir() && !fi.isSymLink()) {
            scanDirectoryRecursive(fi.absoluteFilePath(), seenBasenames, visitedInodes);
        } else if (fi.isFile() && fi.fileName().endsWith(QStringLiteral(".desktop"))) {
            const QString baseName = fi.fileName();
            // Higher precedence directory encountered first wins (shadowing)
            if (!seenBasenames.contains(baseName)) {
                seenBasenames[baseName] = fi.absoluteFilePath();
            }
        }
    }
}

QString AppIndexer::cleanExecCommand(const QString &rawExec) {
    if (rawExec.isEmpty()) return QString();
    QString cmd = rawExec;
    // Freedesktop field codes: %f, %F, %u, %U, %d, %D, %n, %N, %i, %c, %k, %v, %m
    static const QRegularExpression fieldCodeRegex(QStringLiteral("%[a-zA-Z]"));
    cmd.remove(fieldCodeRegex);
    return cmd.simplified();
}

QString AppIndexer::categorizeApp(const QString &categoriesStr) {
    // 8-tier canonical categorization in strict priority order (per reference bridge.py:306-322)
    if (categoriesStr.contains(QStringLiteral("Game"), Qt::CaseInsensitive)) {
        return QStringLiteral("games");
    }

    static const QStringList internetKeys = {
        QStringLiteral("Network"), QStringLiteral("WebBrowser"), QStringLiteral("Email"),
        QStringLiteral("Chat"), QStringLiteral("Feed"), QStringLiteral("FileTransfer"), QStringLiteral("IRCClient")
    };
    for (const auto &k : internetKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("internet");
    }

    static const QStringList devKeys = {
        QStringLiteral("Development"), QStringLiteral("IDE"), QStringLiteral("Debugger"),
        QStringLiteral("Building"), QStringLiteral("RevisionControl"), QStringLiteral("Translation")
    };
    for (const auto &k : devKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("development");
    }

    static const QStringList mediaKeys = {
        QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video"),
        QStringLiteral("Player"), QStringLiteral("Recorder"), QStringLiteral("Music"), QStringLiteral("Midi")
    };
    for (const auto &k : mediaKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("multimedia");
    }

    static const QStringList graphicsKeys = {
        QStringLiteral("Graphics"), QStringLiteral("Photography"), QStringLiteral("RasterGraphics"),
        QStringLiteral("VectorGraphics"), QStringLiteral("2DGraphics"), QStringLiteral("3DGraphics")
    };
    for (const auto &k : graphicsKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("graphics");
    }

    static const QStringList officeKeys = {
        QStringLiteral("Office"), QStringLiteral("Viewer"), QStringLiteral("WordProcessor"),
        QStringLiteral("Spreadsheet"), QStringLiteral("Presentation"), QStringLiteral("Finance")
    };
    for (const auto &k : officeKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("office");
    }

    static const QStringList systemKeys = {
        QStringLiteral("System"), QStringLiteral("Settings"), QStringLiteral("Security"),
        QStringLiteral("Monitor"), QStringLiteral("Core"), QStringLiteral("FileManager"), QStringLiteral("TerminalEmulator")
    };
    for (const auto &k : systemKeys) {
        if (categoriesStr.contains(k, Qt::CaseInsensitive)) return QStringLiteral("system");
    }

    return QStringLiteral("utilities");
}

bool AppIndexer::parseDesktopFile(const QString &filePath, AppEntry &outEntry, const QString &preferredLang) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    // Guard against 0-byte or binary corrupt files
    const QByteArray rawBytes = file.readAll();
    file.close();
    if (rawBytes.size() < 10) return false;

    // Quick UTF-8 validity check
    QString content = QString::fromUtf8(rawBytes);
    if (content.contains(QChar::ReplacementCharacter)) {
        return false;
    }

    const QString fileName = QFileInfo(filePath).fileName();
    const bool isKcm = fileName.startsWith(QStringLiteral("kcm_"));

    outEntry = AppEntry();
    outEntry.id = fileName.chopped(8); // strip ".desktop"
    outEntry.desktopPath = filePath;
    outEntry.isKcm = isKcm;

    bool inDesktopEntry = false;
    DesktopAction *currentAction = nullptr;
    const QString targetLangKey = QStringLiteral("Name[") + preferredLang + QStringLiteral("]");
    const QString genericLangKey = QStringLiteral("GenericName[") + preferredLang + QStringLiteral("]");
    const QString commentLangKey = QStringLiteral("Comment[") + preferredLang + QStringLiteral("]");
    const QString keywordsLangKey = QStringLiteral("Keywords[") + preferredLang + QStringLiteral("]");

    QString rawCategoriesStr;
    QString rawKeywordsStr;
    QString rawKeywordsTrStr;

    const auto lines = QStringView(content).split(u'\n');
    for (auto line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        if (line.startsWith(u'[') && line.endsWith(u']')) {
            if (line == u"[Desktop Entry]") {
                inDesktopEntry = true;
                currentAction = nullptr;
            } else if (line.startsWith(u"[Desktop Action ")) {
                inDesktopEntry = false;
                const QString actionId = line.mid(16, line.size() - 17).toString().trimmed();
                if (!actionId.isEmpty()) {
                    DesktopAction act;
                    act.id = actionId;
                    outEntry.actions.push_back(std::move(act));
                    currentAction = &outEntry.actions.back();
                }
            } else {
                inDesktopEntry = false;
                currentAction = nullptr;
            }
            continue;
        }

        const qsizetype eqIdx = line.indexOf(u'=');
        if (eqIdx <= 0) continue;

        const QStringView key = line.left(eqIdx).trimmed();
        const QStringView val = line.mid(eqIdx + 1).trimmed();

        if (inDesktopEntry) {
            if (key == u"Type") {
                if (val != u"Application" && !isKcm) return false;
            } else if (key == u"NoDisplay") {
                outEntry.noDisplay = (val.compare(u"true", Qt::CaseInsensitive) == 0);
            } else if (key == u"Hidden") {
                outEntry.hidden = (val.compare(u"true", Qt::CaseInsensitive) == 0);
            } else if (key == u"Name") {
                outEntry.labelEn = val.toString();
            } else if (key == targetLangKey) {
                outEntry.labelTr = val.toString();
            } else if (key == u"GenericName") {
                if (outEntry.genericName.isEmpty()) outEntry.genericName = val.toString();
            } else if (key == genericLangKey) {
                outEntry.genericName = val.toString();
            } else if (key == u"Comment") {
                if (outEntry.comment.isEmpty()) outEntry.comment = val.toString();
            } else if (key == commentLangKey) {
                outEntry.comment = val.toString();
            } else if (key == u"Icon") {
                outEntry.icon = val.toString();
            } else if (key == u"Exec") {
                outEntry.rawExec = val.toString();
            } else if (key == u"Path") {
                outEntry.workingDir = val.toString();
            } else if (key == u"Terminal") {
                outEntry.terminal = (val.compare(u"true", Qt::CaseInsensitive) == 0);
            } else if (key == u"Categories") {
                rawCategoriesStr = val.toString();
            } else if (key == u"Keywords") {
                rawKeywordsStr = val.toString();
            } else if (key == keywordsLangKey) {
                rawKeywordsTrStr = val.toString();
            }
        } else if (currentAction != nullptr) {
            if (key == u"Name") {
                if (currentAction->name.isEmpty()) currentAction->name = val.toString();
            } else if (key == targetLangKey) {
                currentAction->name = val.toString();
            } else if (key == u"Exec") {
                currentAction->exec = cleanExecCommand(val.toString());
            } else if (key == u"Icon") {
                currentAction->icon = val.toString();
            }
        }
    }

    // Filter criteria
    if (outEntry.hidden) return false;
    if (outEntry.noDisplay && !isKcm) return false;
    if (outEntry.rawExec.isEmpty() && !isKcm) return false;

    // Label & Bilingual Aliases
    outEntry.label = !outEntry.labelTr.isEmpty() ? outEntry.labelTr : outEntry.labelEn;
    if (outEntry.label.isEmpty()) outEntry.label = outEntry.id;

    if (!outEntry.labelEn.isEmpty() && outEntry.labelEn != outEntry.label) {
        outEntry.aliases.append(outEntry.labelEn);
    }
    if (!outEntry.labelTr.isEmpty() && outEntry.labelTr != outEntry.label) {
        outEntry.aliases.append(outEntry.labelTr);
    }
    if (!outEntry.genericName.isEmpty()) outEntry.aliases.append(outEntry.genericName);
    if (!outEntry.comment.isEmpty()) outEntry.aliases.append(outEntry.comment);

    // Split Keywords (semicolon and space separated)
    static const QRegularExpression kwSep(QStringLiteral("[;\\s]+"));
    auto parseKeywords = [](const QString &kwStr, QStringList &dest) {
        if (kwStr.isEmpty()) return;
        const auto parts = kwStr.split(kwSep, Qt::SkipEmptyParts);
        for (const auto &p : parts) {
            if (!dest.contains(p)) dest.append(p);
        }
    };
    parseKeywords(rawKeywordsStr, outEntry.aliases);
    parseKeywords(rawKeywordsTrStr, outEntry.aliases);

    if (isKcm) {
        outEntry.aliases.append({QStringLiteral("settings"), QStringLiteral("ayarlar"),
                                 QStringLiteral("setup"), QStringLiteral("kcm"), QStringLiteral("systemsettings")});
        outEntry.category = QStringLiteral("system");
    } else {
        outEntry.category = categorizeApp(rawCategoriesStr);
    }

    outEntry.cleanedExec = cleanExecCommand(outEntry.rawExec);

    // Build tokenized search terms
    outEntry.searchTerms.append(outEntry.label.toLower());
    for (const auto &alias : outEntry.aliases) {
        outEntry.searchTerms.append(alias.toLower());
    }

    return true;
}

void AppIndexer::rebuildIndex() {
    auto newSnapshot = std::make_shared<AppIndexSnapshot>();

    std::unordered_map<QString, QString> seenBasenames;
    std::unordered_set<uint64_t> visitedInodes;

    for (const auto &dirPath : standardApplicationDirectories()) {
        scanDirectoryRecursive(dirPath, seenBasenames, visitedInodes);
    }

    for (const auto &[baseName, fullPath] : seenBasenames) {
        Q_UNUSED(baseName)
        AppEntry entry;
        if (parseDesktopFile(fullPath, entry)) {
            newSnapshot->allApps.push_back(std::move(entry));
        }
    }

    // Alphabetical sort by label case-insensitively
    std::sort(newSnapshot->allApps.begin(), newSnapshot->allApps.end(),
              [](const AppEntry &a, const AppEntry &b) {
                  return a.label.localeAwareCompare(b.label) < 0;
              });

    // Populate lookups and category partitions
    QVariantMap countMap;
    const QStringList standardCats = {
        QStringLiteral("games"), QStringLiteral("internet"), QStringLiteral("development"),
        QStringLiteral("multimedia"), QStringLiteral("graphics"), QStringLiteral("office"),
        QStringLiteral("system"), QStringLiteral("utilities")
    };
    for (const auto &c : standardCats) countMap[c] = 0;
    countMap[QStringLiteral("all")] = static_cast<int>(newSnapshot->allApps.size());

    for (size_t i = 0; i < newSnapshot->allApps.size(); ++i) {
        const auto &app = newSnapshot->allApps[i];
        newSnapshot->idLookup[app.id] = i;
        newSnapshot->desktopPathLookup[app.desktopPath] = i;

        newSnapshot->categoryIndices[app.category].push_back(i);
        newSnapshot->categoryIndices[QStringLiteral("all")].push_back(i);

        if (countMap.contains(app.category)) {
            countMap[app.category] = countMap[app.category].toInt() + 1;
        } else {
            countMap[QStringLiteral("utilities")] = countMap[QStringLiteral("utilities")].toInt() + 1;
        }
    }

    // Pre-serialize category JSON strings
    auto buildCatJson = [&](const std::vector<size_t> &indices) -> QString {
        QJsonArray arr;
        for (size_t idx : indices) {
            const auto &app = newSnapshot->allApps[idx];
            QJsonObject obj;
            obj[QStringLiteral("id")] = app.id;
            obj[QStringLiteral("label")] = app.label;
            obj[QStringLiteral("icon")] = app.icon;
            obj[QStringLiteral("desktop")] = app.desktopPath;
            obj[QStringLiteral("category")] = app.category;
            QJsonArray aliasArr;
            for (const auto &al : app.aliases) aliasArr.append(al);
            obj[QStringLiteral("aliases")] = aliasArr;
            arr.append(obj);
        }
        return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    };

    for (const auto &c : standardCats) {
        newSnapshot->cachedCategoryJson[c] = buildCatJson(newSnapshot->categoryIndices[c]);
    }
    newSnapshot->cachedCategoryJson[QStringLiteral("all")] = buildCatJson(newSnapshot->categoryIndices[QStringLiteral("all")]);

    QJsonObject countObj;
    for (auto it = countMap.begin(); it != countMap.end(); ++it) {
        countObj[it.key()] = it.value().toInt();
    }
    newSnapshot->cachedCategoryCountsJson = QString::fromUtf8(QJsonDocument(countObj).toJson(QJsonDocument::Compact));

    // Atomic snapshot swap (zero reader lock contention)
    m_snapshot.store(std::const_pointer_cast<const AppIndexSnapshot>(newSnapshot));
    emit indexUpdated();
}

QString AppIndexer::listAppsCategoryJson(const QString &cat) const {
    auto snapshot = m_snapshot.load();
    if (!snapshot) return QStringLiteral("[]");

    const QString key = (cat.isEmpty() || cat == QStringLiteral("all")) ? QStringLiteral("all") : cat;
    auto it = snapshot->cachedCategoryJson.find(key);
    return it != snapshot->cachedCategoryJson.end() ? it->second : QStringLiteral("[]");
}

QString AppIndexer::getAppCategoryCountsJson() const {
    auto snapshot = m_snapshot.load();
    return snapshot ? snapshot->cachedCategoryCountsJson : QStringLiteral("{}");
}

QString AppIndexer::getAppActionsJson(const QString &desktopPath) const {
    auto snapshot = m_snapshot.load();
    if (!snapshot) return QStringLiteral("[]");

    auto it = snapshot->desktopPathLookup.find(desktopPath);
    if (it == snapshot->desktopPathLookup.end()) return QStringLiteral("[]");

    const auto &app = snapshot->allApps[it->second];
    QJsonArray arr;
    for (const auto &act : app.actions) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = act.id;
        obj[QStringLiteral("name")] = act.name;
        obj[QStringLiteral("exec")] = act.exec;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool AppIndexer::activateExistingWindow(const QString &desktopOrExec) const {
    const QString trimmed = desktopOrExec.trimmed();
    if (trimmed.isEmpty()) return false;

    QString appLabel;
    QString appId;
    QString appIcon;

    const QFileInfo fi(trimmed);
    QString baseName = fi.completeBaseName().toLower();
    if (baseName.endsWith(QStringLiteral(".desktop"))) {
        baseName.chop(8);
    }

    auto snapshot = m_snapshot.load();
    if (snapshot) {
        auto it = snapshot->desktopPathLookup.find(trimmed);
        if (it != snapshot->desktopPathLookup.end()) {
            const auto &app = snapshot->allApps[it->second];
            appLabel = app.label.toLower();
            appId = app.id.toLower();
            appIcon = app.icon.toLower();
        } else {
            auto itId = snapshot->idLookup.find(baseName);
            if (itId != snapshot->idLookup.end()) {
                const auto &app = snapshot->allApps[itId->second];
                appLabel = app.label.toLower();
                appId = app.id.toLower();
                appIcon = app.icon.toLower();
            }
        }
    }

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Match")
    );
    msg << QString(); // query all open windows

    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.arguments().isEmpty()) return false;

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return false;

    arg.beginArray();
    QString foundMatchId;
    while (!arg.atEnd()) {
        arg.beginStructure();
        QString id;
        QString text;
        QString iconName;
        int type;
        double relevance;
        QVariantMap properties;
        arg >> id >> text >> iconName >> type >> relevance >> properties;
        arg.endStructure();

        if (!foundMatchId.isEmpty()) continue;

        const QString tLow = text.toLower();
        const QString iLow = iconName.toLower();

        bool iconMatch = (!appIcon.isEmpty() && (appIcon == iLow || (appIcon.length() >= 4 && (iLow.contains(appIcon) || appIcon.contains(iLow)))));
        if (!iconMatch && !baseName.isEmpty()) {
            iconMatch = (baseName == iLow || (baseName.length() >= 4 && (iLow.contains(baseName) || baseName.contains(iLow))));
        }
        if (!iconMatch && !appId.isEmpty()) {
            iconMatch = (appId == iLow || (appId.length() >= 4 && (iLow.contains(appId) || appId.contains(iLow))));
        }

        bool titleMatch = false;
        if (!appLabel.isEmpty() && appLabel.length() >= 4 && tLow.contains(appLabel)) {
            titleMatch = true;
        } else if (!appId.isEmpty() && appId.length() >= 4 && tLow.contains(appId)) {
            titleMatch = true;
        } else if (!baseName.isEmpty() && baseName.length() >= 4 && tLow.contains(baseName)) {
            titleMatch = true;
        }

        if (iconMatch || titleMatch) {
            foundMatchId = id;
        }
    }
    arg.endArray();

    if (!foundMatchId.isEmpty()) {
        auto runMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/WindowsRunner"),
            QStringLiteral("org.kde.krunner1"),
            QStringLiteral("Run")
        );
        runMsg << foundMatchId << QString();
        QDBusConnection::sessionBus().call(runMsg);
        return true;
    }

    return false;
}

bool AppIndexer::activateTrayApp(const QString &desktopOrExec) const {
    const QString trimmed = desktopOrExec.trimmed();
    if (trimmed.isEmpty()) return false;

    QString appLabel;
    QString appId;
    QString appIcon;
    QString cleanedExec;

    const QFileInfo fi(trimmed);
    QString baseName = fi.completeBaseName().toLower();
    if (baseName.endsWith(QStringLiteral(".desktop"))) {
        baseName.chop(8);
    }

    auto snapshot = m_snapshot.load();
    if (snapshot) {
        auto it = snapshot->desktopPathLookup.find(trimmed);
        if (it != snapshot->desktopPathLookup.end()) {
            const auto &app = snapshot->allApps[it->second];
            appLabel = app.label;
            appId = app.id;
            appIcon = app.icon;
            cleanedExec = app.cleanedExec;
        } else {
            auto itId = snapshot->idLookup.find(baseName);
            if (itId != snapshot->idLookup.end()) {
                const auto &app = snapshot->allApps[itId->second];
                appLabel = app.label;
                appId = app.id;
                appIcon = app.icon;
                cleanedExec = app.cleanedExec;
            }
        }
    }

    auto normalizeKey = [](const QString &s) -> QString {
        QString res;
        res.reserve(s.size());
        for (const QChar &ch : s) {
            if (ch.isLetterOrNumber()) {
                res.append(ch.toLower());
            }
        }
        return res;
    };

    static const std::unordered_set<QString> stopwords = {
        QStringLiteral("org"), QStringLiteral("com"), QStringLiteral("net"),
        QStringLiteral("io"), QStringLiteral("kde"), QStringLiteral("gnome"),
        QStringLiteral("desktop"), QStringLiteral("application"), QStringLiteral("app"),
        QStringLiteral("client"), QStringLiteral("bin")
    };

    std::vector<QString> candidates;
    auto addCandidate = [&](const QString &str) {
        if (str.isEmpty()) return;
        const QString norm = normalizeKey(str);
        if (norm.length() >= 3 && stopwords.find(norm) == stopwords.end()) {
            if (std::find(candidates.begin(), candidates.end(), norm) == candidates.end()) {
                candidates.push_back(norm);
            }
        }
        const QStringList parts = str.split(QRegularExpression(QStringLiteral("[._\\-\\s]+")), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            const QString pNorm = normalizeKey(p);
            if (pNorm.length() >= 3 && stopwords.find(pNorm) == stopwords.end()) {
                if (std::find(candidates.begin(), candidates.end(), pNorm) == candidates.end()) {
                    candidates.push_back(pNorm);
                }
            }
        }
    };

    addCandidate(appLabel);
    addCandidate(appId);
    addCandidate(appIcon);
    addCandidate(cleanedExec);
    addCandidate(baseName);
    addCandidate(fi.baseName());

    if (candidates.empty()) return false;

    QDBusInterface watcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                           QStringLiteral("/StatusNotifierWatcher"),
                           QStringLiteral("org.kde.StatusNotifierWatcher"),
                           QDBusConnection::sessionBus());
    if (!watcher.isValid()) return false;

    const QStringList items = watcher.property("RegisteredStatusNotifierItems").toStringList();
    if (items.isEmpty()) return false;

    for (const QString &item : items) {
        int slash = item.indexOf(QLatin1Char('/'));
        QString svc = slash != -1 ? item.left(slash) : item;
        QString path = slash != -1 ? item.mid(slash) : QStringLiteral("/StatusNotifierItem");

        QDBusInterface sni(svc, path, QStringLiteral("org.kde.StatusNotifierItem"), QDBusConnection::sessionBus());
        if (!sni.isValid()) continue;

        const QString idStr = sni.property("Id").toString();
        const QString titleStr = sni.property("Title").toString();
        const QString iconStr = sni.property("IconName").toString();

        const QString normId = normalizeKey(idStr);
        const QString normTitle = normalizeKey(titleStr);
        const QString normIcon = normalizeKey(iconStr);

        bool matched = false;
        for (const QString &cand : candidates) {
            for (const QString &target : {normId, normTitle, normIcon}) {
                if (target.isEmpty()) continue;
                if (cand == target) {
                    matched = true;
                    break;
                }
                if (cand.length() >= 4 && target.contains(cand)) {
                    matched = true;
                    break;
                }
                if (target.length() >= 4 && cand.contains(target)) {
                    matched = true;
                    break;
                }
            }
            if (matched) break;
        }

        if (matched) {
            sni.call(QStringLiteral("Activate"), 0, 0);
            return true;
        }
    }

    return false;
}

bool AppIndexer::activateTrayOrWindow(const QString &desktopOrExec) const {
    const QString trimmed = desktopOrExec.trimmed();
    if (trimmed.isEmpty()) return false;

    bool trayActivated = activateTrayApp(trimmed);
    bool winActivated = activateExistingWindow(trimmed);

    return trayActivated || winActivated;
}

void AppIndexer::launchApp(const QString &desktopOrExec) {
    const QString trimmed = desktopOrExec.trimmed();
    if (trimmed.isEmpty()) return;

    if (activateTrayOrWindow(trimmed)) {
        return;
    }

    const bool hasSystemdRun = !QStandardPaths::findExecutable(QStringLiteral("systemd-run")).isEmpty();

    if (trimmed.endsWith(QStringLiteral(".desktop")) && QFile::exists(trimmed)) {
        QFileInfo fi(trimmed);
        const QString desktopFileName = fi.fileName();
        QString desktopId = desktopFileName;
        if (desktopId.endsWith(QStringLiteral(".desktop"))) {
            desktopId.chop(8);
        }

        // Tier 1: gtk-launch (standard Freedesktop launcher on modern Linux)
        if (!QStandardPaths::findExecutable(QStringLiteral("gtk-launch")).isEmpty()) {
            if (hasSystemdRun && QProcess::startDetached(QStringLiteral("systemd-run"), {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("gtk-launch"), desktopFileName})) {
                return;
            }
            if (QProcess::startDetached(QStringLiteral("gtk-launch"), {desktopFileName})) {
                return;
            }
            if (hasSystemdRun && QProcess::startDetached(QStringLiteral("systemd-run"), {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("gtk-launch"), desktopId})) {
                return;
            }
            if (QProcess::startDetached(QStringLiteral("gtk-launch"), {desktopId})) {
                return;
            }
        }

        // Tier 2: KDE kioclient with file:// URL scheme (required by KIO on Plasma 6)
        if (!QStandardPaths::findExecutable(QStringLiteral("kioclient")).isEmpty()) {
            const QString fileUrl = QUrl::fromLocalFile(trimmed).toString();
            if (hasSystemdRun && QProcess::startDetached(QStringLiteral("systemd-run"), {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("kioclient"), QStringLiteral("exec"), fileUrl})) {
                return;
            }
            if (QProcess::startDetached(QStringLiteral("kioclient"), {QStringLiteral("exec"), fileUrl})) {
                return;
            }
        }

        // Tier 3: Direct execution using parsed Exec= command from AppIndexer snapshot
        auto snapshot = m_snapshot.load();
        if (snapshot) {
            auto it = snapshot->desktopPathLookup.find(trimmed);
            if (it != snapshot->desktopPathLookup.end()) {
                const auto &app = snapshot->allApps[it->second];
                if (app.terminal) {
                    runInTerminal(app.cleanedExec);
                } else if (!app.cleanedExec.isEmpty()) {
                    if (hasSystemdRun && QProcess::startDetached(QStringLiteral("systemd-run"), {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), app.cleanedExec})) {
                        return;
                    }
                    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), app.cleanedExec});
                }
                return;
            }
        }
    }

    // Direct exec string (e.g. from test suites "firefox %u" or custom binary)
    const QString cleaned = cleanExecCommand(trimmed);
    if (!cleaned.isEmpty()) {
        if (hasSystemdRun && QProcess::startDetached(QStringLiteral("systemd-run"), {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), cleaned})) {
            return;
        }
        QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cleaned});
    }
}

void AppIndexer::openContainingFolder(const QString &desktopPath) const {
    if (desktopPath.isEmpty()) return;
    const QString folder = QFileInfo(desktopPath).absolutePath();
    if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), desktopPath});
    } else {
        QProcess::startDetached(QStringLiteral("xdg-open"), {folder});
    }
}

void AppIndexer::editDesktopFile(const QString &desktopPath) const {
    if (desktopPath.isEmpty() || !QFile::exists(desktopPath)) return;
    QString editor = QString::fromUtf8(qgetenv("EDITOR"));
    if (editor.isEmpty()) editor = QStringLiteral("kate");
    if (QStandardPaths::findExecutable(editor).isEmpty()) editor = QStringLiteral("xdg-open");
    QProcess::startDetached(editor, {desktopPath});
}

void AppIndexer::openInDiscover(const QString &desktopPath) const {
    const QString baseName = QFileInfo(desktopPath).fileName();
    if (!QStandardPaths::findExecutable(QStringLiteral("plasma-discover")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-discover"), {QStringLiteral("--launch"), baseName});
    }
}

void AppIndexer::runInTerminal(const QString &desktopOrCmd) const {
    const QString cmd = cleanExecCommand(desktopOrCmd);
    if (cmd.isEmpty()) return;

    QString term = QString::fromUtf8(qgetenv("TERMINAL"));
    if (term.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("konsole"), QStringLiteral("kitty"),
            QStringLiteral("alacritty"), QStringLiteral("foot"),
            QStringLiteral("wezterm"), QStringLiteral("xterm")
        };
        for (const auto &c : candidates) {
            if (!QStandardPaths::findExecutable(c).isEmpty()) {
                term = c;
                break;
            }
        }
    }
    if (term.isEmpty()) term = QStringLiteral("konsole");

    QString escaped = cmd;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    if (term == QLatin1String("konsole")) {
        QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
                                       escaped + QStringLiteral("; exec bash")});
    } else if (term == QLatin1String("kitty")) {
        QProcess::startDetached(term, {QStringLiteral("--hold"), QStringLiteral("sh"), QStringLiteral("-c"), escaped});
    } else if (term == QLatin1String("alacritty")) {
        QProcess::startDetached(term, {QStringLiteral("--hold"), QStringLiteral("-e"), QStringLiteral("sh"), QStringLiteral("-c"), escaped});
    } else {
        QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                                       escaped + QStringLiteral("; exec $SHELL")});
    }
}

QString AppIndexer::resolveAppIcon(const QString &appId) const {
    auto snapshot = m_snapshot.load();
    if (!snapshot) return QString();

    // 1. Direct match on id
    auto it = snapshot->idLookup.find(appId);
    if (it != snapshot->idLookup.end()) {
        return snapshot->allApps[it->second].icon;
    }

    // 2. Strip common desktop prefix/suffix if present
    QString clean = appId;
    if (clean.endsWith(QStringLiteral(".desktop"))) {
        clean.chop(8);
    }
    it = snapshot->idLookup.find(clean);
    if (it != snapshot->idLookup.end()) {
        return snapshot->allApps[it->second].icon;
    }

    return QString();
}

std::vector<AppSearchResult> AppIndexer::searchApps(const QString &query,
                                                    const QString &category,
                                                    size_t maxResults) const {
    auto snapshot = m_snapshot.load();
    if (!snapshot) return {};

    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) return {};

    const bool filterCategory = (!category.isEmpty() && category != QStringLiteral("all"));
    std::vector<AppSearchResult> results;

    const auto &candidateIndices = filterCategory && snapshot->categoryIndices.contains(category)
        ? snapshot->categoryIndices.at(category)
        : snapshot->categoryIndices.at(QStringLiteral("all"));

    for (size_t order = 0; order < candidateIndices.size(); ++order) {
        const auto &app = snapshot->allApps[candidateIndices[order]];

        const QString labelLower = app.label.toLower();
        int baseScore = 80;

        if (labelLower == needle) {
            baseScore = 0;
        } else if (labelLower.split(QLatin1Char(' ')).contains(needle)) {
            baseScore = 0;
        } else if (labelLower.startsWith(needle)) {
            baseScore = 10;
        } else if (labelLower.contains(needle)) {
            baseScore = 30;
        } else {
            // Check aliases
            bool aliasMatch = false;
            for (const auto &alias : app.aliases) {
                if (alias.toLower().contains(needle)) {
                    aliasMatch = true;
                    break;
                }
            }
            if (aliasMatch) {
                baseScore = 40;
            } else if (!app.comment.isEmpty() && app.comment.toLower().contains(needle)) {
                baseScore = 60;
            } else {
                continue; // No match
            }
        }

        // App deduction bonus (-5)
        baseScore -= 5;

        // Ranking score formula: score * 1000 + depth * 25 + order
        const int finalScore = baseScore * 1000 + 0 * 25 + static_cast<int>(order);
        results.push_back({&app, finalScore});
    }

    std::sort(results.begin(), results.end(), [](const AppSearchResult &a, const AppSearchResult &b) {
        return a.score < b.score;
    });

    if (results.size() > maxResults) {
        results.resize(maxResults);
    }

    return results;
}

} // namespace ravenspot
