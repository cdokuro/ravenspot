#include "ShellBridge.hpp"
#include "ThemeManager.hpp"
#include "../audio/AudioEngine.hpp"
#include "../core/LayerShellWindow.hpp"
#include "../search/AppIndexer.hpp"
#include "../run/CommandAnalyzer.hpp"
#include "../run/CommandRunner.hpp"
#include "../run/CommandHistory.hpp"
#include "pc/ClipboardHistory.hpp"
#include "pc/ProcessList.hpp"
#include "pc/RecentFiles.hpp"
#include "net/VideoDownload.hpp"

#include <QGuiApplication>
#include <QMimeData>
#include <QClipboard>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <algorithm>

namespace ravenspot {

namespace platform { void trimHeap(); } // platform dosyalarında tanımlı

ShellBridge::ShellBridge(bool startHidden, QObject *parent)
    : QObject(parent)
    , m_startHidden(startHidden)
{
    m_telemetryTimer.setInterval(1000);
    connect(&m_telemetryTimer, &QTimer::timeout, this, &ShellBridge::updateTelemetry);

    m_commandRunner = new CommandRunner(this);
    m_commandHistory = new CommandHistory(CommandHistory::defaultPath(), this);
    m_clipHistory = new ClipboardHistory(this);
    initPlatform(); // ShellBridge_linux.cpp / ShellBridge_win.cpp
}

ShellBridge::~ShellBridge() {
    if (m_sleepInhibitCookie != 0) {
        setSleepInhibited(false);
    }
}

void ShellBridge::setAppIndexer(AppIndexer *indexer) {
    m_appIndexer = indexer;
}

void ShellBridge::setLayerShellWindow(LayerShellWindow *window) {
    m_layerShellWindow = window;
    if (m_layerShellWindow) {
        connect(m_layerShellWindow.data(), &LayerShellWindow::visibilityChanged, this, [this](bool visible) {
            onMenuVisibilityChanged(visible);
        });
    }
}

void ShellBridge::setThemeManager(ThemeManager *themeMgr) {
    m_themeManager = themeMgr;
    if (m_themeManager) {
        connect(m_themeManager, &ThemeManager::themeChanged, this, [this](const QString &) {
            emit themeChanged();
        });
    }
}

void ShellBridge::setAudioEngine(AudioEngine *audioEngine) {
    m_audioEngine = audioEngine;
    if (m_audioEngine) {
        connect(m_audioEngine, &AudioEngine::audioConfigChanged, this, [this](int vol, bool muted) {
            emit audioConfigChanged(vol, muted);
            emit appAudioChanged(vol, muted);
        });
    }
}

QString ShellBridge::version() const {
    return QStringLiteral("0.3.0");
}

// ── 1. Window & Lifecycle ──────────────────────────────────

void ShellBridge::centerWindowOnActiveScreen(QObject *qquickWindow) {
    Q_UNUSED(qquickWindow)
    if (m_layerShellWindow) {
        m_layerShellWindow->showOnActiveScreen();
    }
}

void ShellBridge::onMenuVisibilityChanged(bool visible) {
    if (!visible) {
        m_execCache.clear();
        // Open With listesi menü kapanınca yeniden taranır: sonradan kurulan uygulamalar görünsün
        m_desktopApps.clear();
        m_desktopAppsLoaded = false;
    }
    if (visible) {
        if (!m_telemetryTimer.isActive()) {
            m_telemetryTimer.start();
        }
        // Do not block initial frame rendering with synchronous sysfs/proc reads;
        // let telemetry update on next event loop tick or timer interval.
        QTimer::singleShot(50, this, &ShellBridge::updateTelemetry);
    } else {
        m_telemetryTimer.stop();
        platform::trimHeap();
    }
}

void ShellBridge::toggleMenu() {
    emit toggleRequested();
}

void ShellBridge::showMenu() {
    emit showRequested();
}

void ShellBridge::hideMenu() {
    emit hideRequested();
}

void ShellBridge::beginDrag(double initialDx, double initialDy) {
    if (m_layerShellWindow) {
        m_layerShellWindow->beginDrag(initialDx, initialDy);
    }
}

QPoint ShellBridge::dragBy(double deltaX, double deltaY) {
    if (m_layerShellWindow) {
        return m_layerShellWindow->dragBy(deltaX, deltaY);
    }
    return QPoint(0, 0);
}

void ShellBridge::updateDrag(double localX, double localY) {
    if (m_layerShellWindow) {
        m_layerShellWindow->updateDrag(localX, localY);
    }
}

bool ShellBridge::isSwitchingScreen() const {
    return m_layerShellWindow && m_layerShellWindow->isSwitchingScreen();
}

void ShellBridge::setCardHeight(int height) {
    if (m_layerShellWindow) {
        m_layerShellWindow->setCardHeight(height);
    }
}

void ShellBridge::endDrag() {
    if (m_layerShellWindow) {
        m_layerShellWindow->endDrag();
    }
}

// ── 2. Audio & Volume ──────────────────────────────────────

void ShellBridge::playThock() {
    if (m_audioEngine) {
        m_audioEngine->playThock();
    }
}

void ShellBridge::playCreamy() {
    if (m_audioEngine) {
        m_audioEngine->playCreamy();
    }
}

void ShellBridge::playSound(const QString &soundType) {
    if (m_audioEngine) {
        m_audioEngine->play(soundType);
    }
}

void ShellBridge::setAppVolume(int vol) {
    if (m_audioEngine) {
        m_audioEngine->setVolume(vol);
    }
}

void ShellBridge::setAudioVolume(int vol) {
    setAppVolume(vol);
}

void ShellBridge::toggleAppMute() {
    if (m_audioEngine) {
        m_audioEngine->toggleMute();
    }
}

void ShellBridge::setAudioMuted(bool muted) {
    if (m_audioEngine) {
        m_audioEngine->setMuted(muted);
    }
}

QVariantMap ShellBridge::getAppAudioInfo() const {
    QVariantMap map;
    if (m_audioEngine) {
        map[QStringLiteral("volume")] = m_audioEngine->volume();
        map[QStringLiteral("muted")] = m_audioEngine->isMuted();
    } else {
        map[QStringLiteral("volume")] = 10;
        map[QStringLiteral("muted")] = false;
    }
    return map;
}

QVariantMap ShellBridge::getInitialAudioInfo() const {
    QVariantMap map;
    map[QStringLiteral("volume")] = 50;
    map[QStringLiteral("muted")] = false;
    return map;
}


// ── 3. Themes Subsystem ────────────────────────────────────

QVariantMap ShellBridge::currentTheme() const {
    return m_themeManager ? m_themeManager->currentTheme() : QVariantMap();
}

QString ShellBridge::listRavenThemes() const {
    return m_themeManager ? m_themeManager->listRavenThemes() : QStringLiteral("[]");
}

void ShellBridge::applyRavenTheme(const QString &key) {
    if (m_themeManager) {
        m_themeManager->applyTheme(key);
    }
}

bool ShellBridge::applyTheme(const QString &key) {
    return m_themeManager ? m_themeManager->applyTheme(key) : false;
}

QString ShellBridge::listColorschemes() const {
    return QStringLiteral("[]");
}


bool ShellBridge::toggleFavoriteTheme(const QString &name) {
    Q_UNUSED(name)
    return false;
}

// ── 4. File, Path & Config Operations ──────────────────────

QString ShellBridge::readFile(const QString &path) const {
    if (path.isEmpty()) return QString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString ShellBridge::menuPath() const {
    // Check cwd
    const QString cwd = QDir::currentPath() + QStringLiteral("/menu.jsonc");
    if (QFile::exists(cwd)) return cwd;

    // Check appDir
    const QString app = QCoreApplication::applicationDirPath() + QStringLiteral("/menu.jsonc");
    if (QFile::exists(app)) return app;

    // Check system
    const QString sys = QStringLiteral("/usr/share/ravenspot/menu.jsonc");
    if (QFile::exists(sys)) return sys;

    // Reference
    return QStringLiteral("/home/dokuro/Masaüstü/ravenspot/menu.jsonc");
}

QString ShellBridge::userMenuPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot/menu.jsonc");
}

bool ShellBridge::isDirectory(const QString &path) const {
    return QFileInfo(path).isDir();
}

QString ShellBridge::resolveItemPath(const QString &itemId, const QString &provider,
                                    const QString &action, const QString &value) const {
    Q_UNUSED(action);
    if (!value.isEmpty()) return value;

    if (itemId == QStringLiteral("downloads") || provider == QStringLiteral("files-downloads")) {
        return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (provider.startsWith(QStringLiteral("files-"))) {
        const QString folderName = provider.mid(6);
        if (folderName == QStringLiteral("downloads")) {
            return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        } else if (folderName == QStringLiteral("documents")) {
            return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        } else if (folderName == QStringLiteral("pictures")) {
            return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        } else if (folderName == QStringLiteral("music")) {
            return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        } else if (folderName == QStringLiteral("videos")) {
            return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        } else {
            return QDir::homePath() + QStringLiteral("/") + folderName;
        }
    }
    return QString();
}

// Steam kütüphaneleri: libraryfolders.vdf'teki her "path" + /steamapps/common (var olanlar)
static QStringList steamCommonDirs() {
#ifdef Q_OS_WIN
    const QString steamRoot = QStringLiteral("C:/Program Files (x86)/Steam");
#else
    const QString steamRoot = QDir::homePath() + QStringLiteral("/.local/share/Steam");
#endif
    QStringList libs = {steamRoot};
    QFile vdf(steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"));
    if (vdf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression pathRe(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
        auto it = pathRe.globalMatch(QString::fromUtf8(vdf.readAll()));
        while (it.hasNext()) {
            QString lib = it.next().captured(1);
#ifdef Q_OS_WIN
            lib.replace(QStringLiteral("\\\\"), QStringLiteral("/")); // vdf escapes backslashes
#endif
            libs << lib;
        }
    }
    QStringList dirs;
    for (const QString &lib : libs) {
        const QString common = QFileInfo(lib + QStringLiteral("/steamapps/common")).canonicalFilePath();
        if (!common.isEmpty() && !dirs.contains(common)) dirs << common;
    }
    return dirs;
}

// "Steam Library": tüm kütüphanelerin oyun klasörleri, alfabetik (Proton/çalışma
// zamanı/Steamworks gibi araç klasörleri ve dosyalar hariç)
static QString steamLibraryJson() {
    static const QRegularExpression toolDir(QStringLiteral(
        "^(Proton|Steamworks Shared|SteamLinuxRuntime|Steam Linux Runtime|log$)"));
    QFileInfoList games;
    for (const QString &d : steamCommonDirs()) {
        for (const QFileInfo &fi : QDir(d).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!toolDir.match(fi.fileName()).hasMatch()) games << fi;
        }
    }
    std::sort(games.begin(), games.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
    });
    QJsonArray arr;
    for (const QFileInfo &fi : games) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = fi.fileName();
        obj[QStringLiteral("path")] = fi.absoluteFilePath();
        obj[QStringLiteral("is_dir")] = true;
        obj[QStringLiteral("desc")] = fi.absolutePath(); // hangi kütüphanede
        obj[QStringLiteral("iconName")] = QStringLiteral("folder-games");
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ShellBridge::listFolderFiles(const QString &folderName) const {
    if (folderName == QStringLiteral("steam")) return steamLibraryJson();
    QString targetDir;
    if (folderName == QStringLiteral("downloads")) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    } else if (folderName == QStringLiteral("documents")) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    } else if (folderName == QStringLiteral("pictures")) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    } else if (folderName == QStringLiteral("music")) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    } else if (folderName == QStringLiteral("videos")) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    } else {
        targetDir = QDir::homePath() + QStringLiteral("/") + folderName;
    }

    QDir dir(targetDir);
    if (!dir.exists()) return QStringLiteral("[]");

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    QJsonArray arr;
    int count = 0;
    for (const auto &fi : entries) {
        if (++count > 50) break;
        QJsonObject obj;
        obj[QStringLiteral("name")] = fi.fileName();
        obj[QStringLiteral("path")] = fi.absoluteFilePath();
        obj[QStringLiteral("is_dir")] = fi.isDir();
        obj[QStringLiteral("desc")] = fi.isDir() ? QStringLiteral("Dizin") : QStringLiteral("%1 KB").arg(fi.size() / 1024);
        obj[QStringLiteral("iconName")] = fi.isDir() ? QStringLiteral("folder") : QStringLiteral("text-plain");
        arr.append(obj);
    }

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}


bool ShellBridge::moveToTrash(const QString &path) {
    if (path.isEmpty()) return false;
    return QFile::moveToTrash(path);
}

static QStringList fileSearchArgs(const QString &pat, const QString &homeDir) {
    return {
        QStringLiteral("-i"), QStringLiteral("-a"),
        QStringLiteral("--max-results"), QStringLiteral("35"),
        QStringLiteral("--exclude"), QStringLiteral(".git"),
        QStringLiteral("--exclude"), QStringLiteral("node_modules"),
        QStringLiteral("--exclude"), QStringLiteral(".cache"),
        QStringLiteral("--exclude"), QStringLiteral(".local/share/Trash"),
        pat,
        homeDir
    };
}

// fd çıktısını (satır başına bir yol) QML JSON satırlarına dönüştürür.
static QString fileSearchOutputToJson(const QByteArray &raw) {
    const QString homeDir = QDir::homePath();
    const QString output = QString::fromUtf8(raw);
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QJsonArray items;
    for (const QString &rawPath : lines) {
        const QString fullPath = QDir::cleanPath(rawPath.trimmed()); // fd klasörleri "/" ile bitirir
        if (fullPath.isEmpty()) continue;

        QFileInfo fi(fullPath);
        if (!fi.exists()) continue;

        bool isDir = fi.isDir();
        QString basename = fi.fileName();
        QString dirPath = fi.dir().path();
        QString shortDir = dirPath;
        if (!homeDir.isEmpty() && shortDir.startsWith(homeDir)) {
            shortDir.replace(0, homeDir.length(), QStringLiteral("~"));
        }

        QString sizeStr;
        if (!isDir) {
            double sz = static_cast<double>(fi.size());
            const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            int unitIdx = 0;
            while (sz >= 1024.0 && unitIdx < 4) {
                sz /= 1024.0;
                ++unitIdx;
            }
            if (unitIdx == 0) {
                sizeStr = QString::number(static_cast<qint64>(sz)) + QStringLiteral(" B");
            } else {
                sizeStr = QString::number(sz, 'f', 1) + QLatin1Char(' ') + QLatin1String(units[unitIdx]);
            }
        }

        QString modStr = fi.lastModified().toString(QStringLiteral("dd.MM.yyyy hh:mm"));

        QString iconName = QStringLiteral("text-plain");
        QString iconGlyph = QStringLiteral("󰈙");

        if (isDir) {
            iconName = QStringLiteral("folder");
            iconGlyph = QStringLiteral("󰝰");
        } else {
            const QString ext = fi.suffix().toLower();
            if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") ||
                ext == QStringLiteral("webp") || ext == QStringLiteral("svg") || ext == QStringLiteral("gif") ||
                ext == QStringLiteral("ico") || ext == QStringLiteral("bmp")) {
                iconName = QStringLiteral("image-x-generic");
                iconGlyph = QStringLiteral("󰋩");
            } else if (ext == QStringLiteral("mp4") || ext == QStringLiteral("mkv") || ext == QStringLiteral("mov") ||
                       ext == QStringLiteral("avi") || ext == QStringLiteral("webm") || ext == QStringLiteral("flv")) {
                iconName = QStringLiteral("video-x-generic");
                iconGlyph = QStringLiteral("󰕼");
            } else if (ext == QStringLiteral("mp3") || ext == QStringLiteral("flac") || ext == QStringLiteral("wav") ||
                       ext == QStringLiteral("ogg") || ext == QStringLiteral("m4a") || ext == QStringLiteral("aac")) {
                iconName = QStringLiteral("audio-x-generic");
                iconGlyph = QStringLiteral("󰕾");
            } else if (ext == QStringLiteral("pdf")) {
                iconName = QStringLiteral("application-pdf");
                iconGlyph = QStringLiteral("󰈦");
            } else if (ext == QStringLiteral("zip") || ext == QStringLiteral("tar") || ext == QStringLiteral("gz") ||
                       ext == QStringLiteral("xz") || ext == QStringLiteral("7z") || ext == QStringLiteral("rar") ||
                       ext == QStringLiteral("bz2") || ext == QStringLiteral("zst")) {
                iconName = QStringLiteral("package-x-generic");
                iconGlyph = QStringLiteral("󰛫");
            } else if (ext == QStringLiteral("doc") || ext == QStringLiteral("docx") || ext == QStringLiteral("odt") || ext == QStringLiteral("rtf")) {
                iconName = QStringLiteral("application-msword");
                iconGlyph = QStringLiteral("󰈬");
            } else if (ext == QStringLiteral("xls") || ext == QStringLiteral("xlsx") || ext == QStringLiteral("ods") || ext == QStringLiteral("csv")) {
                iconName = QStringLiteral("application-vnd.ms-excel");
                iconGlyph = QStringLiteral("󰈛");
            } else if (ext == QStringLiteral("ppt") || ext == QStringLiteral("pptx") || ext == QStringLiteral("odp")) {
                iconName = QStringLiteral("application-vnd.ms-powerpoint");
                iconGlyph = QStringLiteral("󰈧");
            } else if (ext == QStringLiteral("py") || ext == QStringLiteral("js") || ext == QStringLiteral("ts") ||
                       ext == QStringLiteral("html") || ext == QStringLiteral("css") || ext == QStringLiteral("cpp") ||
                       ext == QStringLiteral("c") || ext == QStringLiteral("rs") || ext == QStringLiteral("json") ||
                       ext == QStringLiteral("qml") || ext == QStringLiteral("sh") || ext == QStringLiteral("bash")) {
                iconName = QStringLiteral("text-x-generic");
                iconGlyph = QStringLiteral("󰅩");
            } else if (ext == QStringLiteral("desktop")) {
                iconName = QStringLiteral("system-run");
                iconGlyph = QStringLiteral("󰀻");
            }
        }

        QJsonObject obj;
        obj[QStringLiteral("name")] = basename;
        obj[QStringLiteral("path")] = fullPath;
        obj[QStringLiteral("dir")] = shortDir;
        obj[QStringLiteral("isDir")] = isDir;
        obj[QStringLiteral("size")] = sizeStr;
        obj[QStringLiteral("date")] = modStr;
        obj[QStringLiteral("icon")] = iconGlyph;
        obj[QStringLiteral("iconName")] = iconName;
        items.append(obj);
    }

    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
}

QString ShellBridge::searchFiles(const QString &pattern) const {
    const QString pat = pattern.trimmed();
    if (pat.isEmpty()) return QStringLiteral("[]");

    QProcess proc;
    proc.start(QStringLiteral("fd"), fileSearchArgs(pat, QDir::homePath()));
    if (!proc.waitForFinished(1500)) {
        proc.kill();
        return QStringLiteral("[]");
    }
    return fileSearchOutputToJson(proc.readAllStandardOutput());
}

void ShellBridge::requestFileSearch(const QString &pattern, int requestId) {
    // Önceki aramayı iptal et: yalnızca en güncel sorgu önemlidir.
    if (m_fileSearchProc) {
        m_fileSearchProc->disconnect(this);
        m_fileSearchProc->kill();
        m_fileSearchProc->deleteLater();
        m_fileSearchProc = nullptr;
    }

    const QString pat = pattern.trimmed();
    if (pat.isEmpty()) {
        emit fileSearchFinished(requestId, pattern, QStringLiteral("[]"));
        return;
    }

    auto *proc = new QProcess(this);
    m_fileSearchProc = proc;

    auto *timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, proc, [proc]() { proc->kill(); });

    connect(proc, &QProcess::finished, this, [this, proc, pattern, requestId](int, QProcess::ExitStatus status) {
        if (m_fileSearchProc == proc) m_fileSearchProc = nullptr;
        const QString json = (status == QProcess::NormalExit)
            ? fileSearchOutputToJson(proc->readAllStandardOutput())
            : QStringLiteral("[]");
        proc->deleteLater();
        emit fileSearchFinished(requestId, pattern, json);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, pattern, requestId](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return; // diğerlerinde finished da gelir
        if (m_fileSearchProc == proc) m_fileSearchProc = nullptr;
        proc->deleteLater();
        emit fileSearchFinished(requestId, pattern, QStringLiteral("[]"));
    });

    proc->start(QStringLiteral("fd"), fileSearchArgs(pat, QDir::homePath()));
    timeout->start(1500);
}

// ── 5. Application Launching & Execution ───────────────────

QString ShellBridge::listAppsCategory(const QString &cat) const {
    return m_appIndexer ? m_appIndexer->listAppsCategoryJson(cat) : QStringLiteral("[]");
}

QString ShellBridge::getAppCategoryCounts() const {
    return m_appIndexer ? m_appIndexer->getAppCategoryCountsJson() : QStringLiteral("{}");
}


QString ShellBridge::getAppActions(const QString &desktop) const {
    return m_appIndexer ? m_appIndexer->getAppActionsJson(desktop) : QStringLiteral("[]");
}



QVariantMap ShellBridge::fileProperties(const QString &path) const {
    return files::properties(path);
}

QString ShellBridge::renamePath(const QString &path, const QString &newName) {
    return files::renamePath(path, newName);
}

bool ShellBridge::isArchive(const QString &path) const {
    return files::isArchive(path);
}



void ShellBridge::copyFileToClipboard(const QString &path) {
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    auto *mime = new QMimeData;
    mime->setUrls({url});
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), QByteArray("copy\n") + url.toEncoded());
    mime->setText(QFileInfo(path).absoluteFilePath());
    QGuiApplication::clipboard()->setMimeData(mime); // Dolphin'e "yapıştır" ile dosya kopyalanır
}


QVariantMap ShellBridge::analyzeCommand(const QString &text) const {
    return cmd::analyze(text, [this](const QString &tok) {
        auto it = m_execCache.constFind(tok);
        if (it != m_execCache.constEnd()) return it.value();
        const bool found = !QStandardPaths::findExecutable(tok).isEmpty();
        m_execCache.insert(tok, found);
        return found;
    });
}


bool ShellBridge::runCommandInline(const QString &command) {
    if (command.trimmed().isEmpty()) return false;
    if (cmd::needsTerminal(command)) {
        runCommandInTerminal(command); // sudo / etkileşimli program: terminal gerekir
        return false;
    }
    if (!m_commandRunner->start(command)) return false;
    m_commandHistory->add(command);
    return true;
}

void ShellBridge::stopInlineCommand() {
    m_commandRunner->stop();
}

QStringList ShellBridge::commandHistory(const QString &query) const {
    return query.trimmed().isEmpty() ? m_commandHistory->matching(QString(), 10)
                                     : m_commandHistory->matching(query, 5);
}

QObject *ShellBridge::runner() const {
    return m_commandRunner;
}


void ShellBridge::executeCommand(const QString &cmdStr, bool inTerminal) {
    if (cmdStr.trimmed().isEmpty()) return;
    if (inTerminal) {
        runInTerminal(cmdStr);
    } else {
        runDetached(cmdStr);
    }
}


bool ShellBridge::isCommandAvailable(const QString &command) const {
    const QString cmd = command.trimmed();
    if (cmd.isEmpty()) return false;
    if (cmd.startsWith(QLatin1Char('/')) || cmd.startsWith(QLatin1String("~/"))) {
        QString expanded = cmd;
        if (expanded.startsWith(QLatin1String("~/"))) {
            expanded = QDir::homePath() + expanded.mid(1);
        }
        return QFile::exists(expanded);
    }
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

QString ShellBridge::listPowerProfiles() const {
    QJsonArray arr;
    auto addProfile = [&arr, this](const QString &val, const QString &label) {
        QJsonObject obj;
        obj[QStringLiteral("value")] = val;
        obj[QStringLiteral("label")] = label;
        obj[QStringLiteral("current")] = (val == m_powerProfile);
        arr.append(obj);
    };

    addProfile(QStringLiteral("power-saver"), QStringLiteral("Power Saver"));
    addProfile(QStringLiteral("balanced"), QStringLiteral("Balanced"));
    addProfile(QStringLiteral("performance"), QStringLiteral("Performance"));

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void ShellBridge::applyPowerProfile(const QString &profile) {
    setPowerProfile(profile);
}

QString ShellBridge::getPowerProfile() const {
    return m_powerProfile;
}


bool ShellBridge::isSleepInhibited() const {
    return (m_sleepInhibitCookie != 0) || m_sleepInhibited;
}


// ── 7. Clipboard & Process Management ──────────────────────

static QString clipEntriesJson(const QList<ClipEntry> &entries) {
    QJsonArray arr;
    for (const ClipEntry &e : entries) {
        QString label = e.text.trimmed().section(QLatin1Char('\n'), 0, 0).simplified();
        if (label.size() > 90) label = label.left(89) + QChar(0x2026);
        arr.append(QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("value"), e.text},
                               {QStringLiteral("truncated"), e.truncated}});
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ShellBridge::listClipboard() const {
    return clipEntriesJson(m_clipHistory->entries());
}

QString ShellBridge::clipboardMatching(const QString &query) const {
    return clipEntriesJson(m_clipHistory->matching(query, 15));
}


QString ShellBridge::searchEmoji(const QString &query) const {
    if (!m_emoji.isLoaded()) m_emoji.load(m_assetsDir + QStringLiteral("/emoji.json"));
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_emoji.search(query, 20)))
                                 .toJson(QJsonDocument::Compact));
}

void ShellBridge::setClipboard(const QString &text) {
    copyToClipboard(text);
}

void ShellBridge::copyToClipboard(const QString &text) {
    QString sanitized = text;
    sanitized.remove(QChar(0));
    if (auto *cb = QGuiApplication::clipboard()) {
        cb->setText(sanitized);
    }
}


// ── 8. Telemetry & Updates ─────────────────────────────────


// ── 9. Pasted links ────────────────────────────────────────

QString ShellBridge::platformName() const {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

void ShellBridge::openUrl(const QString &url) {
    if (!video::isHttpUrl(url)) return;
    QDesktopServices::openUrl(QUrl(url.trimmed(), QUrl::StrictMode));
}

QString ShellBridge::videoDownloaderPath() const {
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (!inPath.isEmpty()) return inPath;
#ifdef Q_OS_WIN
    // winget/pip installs before the user's PATH is refreshed (new login)
    const QString local = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    const QStringList extra = {
        local + QStringLiteral("/Microsoft/WinGet/Links/yt-dlp.exe"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp.exe"),
    };
#else
    const QStringList extra = {QDir::homePath() + QStringLiteral("/.local/bin/yt-dlp")};
#endif
    for (const QString &p : extra) {
        if (QFileInfo(p).isExecutable()) return p;
    }
    return QString();
}

bool ShellBridge::isVideoDownloaderAvailable() const {
    return !videoDownloaderPath().isEmpty();
}

QString ShellBridge::videoDownloaderInstallCommand() const {
#ifdef Q_OS_WIN
    return QStringLiteral("winget install --id yt-dlp.yt-dlp -e --accept-source-agreements --accept-package-agreements");
#else
    return QStringLiteral("sudo pacman -S --noconfirm yt-dlp || yay -S --noconfirm yt-dlp");
#endif
}

bool ShellBridge::downloadVideo(const QString &url) {
    if (!video::isHttpUrl(url)) return false;
    const QString exe = videoDownloaderPath();
    if (exe.isEmpty()) return false;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    QDir().mkpath(dir);
    return m_commandRunner->startProgram(exe, video::downloadArgs(url, dir),
                                         QStringLiteral("yt-dlp ") + url.trimmed());
}

} // namespace ravenspot
