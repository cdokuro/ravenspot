#include "ShellBridge.hpp"
#include "ThemeManager.hpp"
#include "../audio/AudioEngine.hpp"
#include "../core/LayerShellWindow.hpp"
#include "../search/AppIndexer.hpp"

#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusReply>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <sys/statvfs.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>

namespace ravenspot {

ShellBridge::ShellBridge(bool startHidden, QObject *parent)
    : QObject(parent)
    , m_startHidden(startHidden)
{
    m_telemetryTimer.setInterval(1000);
    connect(&m_telemetryTimer, &QTimer::timeout, this, &ShellBridge::updateTelemetry);

    // Sync sleep inhibition state with KDE / FreeDesktop PowerManagement
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
        QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
        QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
        QStringLiteral("HasInhibitChanged"),
        this,
        SIGNAL(sleepInhibitedChanged(bool))
    );

    connect(this, &ShellBridge::sleepInhibitedChanged, this, [this](bool inhibited) {
        m_sleepInhibited = inhibited;
    });

    // Query initial power profile & sleep inhibit state once at startup (0ms overhead during open)
    QTimer::singleShot(0, this, [this]() {
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("net.hadess.PowerProfiles"),
            QStringLiteral("/net/hadess/PowerProfiles"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get")
        );
        msg << QStringLiteral("net.hadess.PowerProfiles") << QStringLiteral("ActiveProfile");
        QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(msg);
        if (reply.isValid()) {
            m_powerProfile = reply.value().variant().toString();
        }

        auto inMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("HasInhibit")
        );
        QDBusReply<bool> inReply = QDBusConnection::sessionBus().call(inMsg);
        if (inReply.isValid()) {
            m_sleepInhibited = inReply.value();
        }
    });
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
    if (visible) {
        if (!m_telemetryTimer.isActive()) {
            m_telemetryTimer.start();
        }
        // Do not block initial frame rendering with synchronous sysfs/proc reads;
        // let telemetry update on next event loop tick or timer interval.
        QTimer::singleShot(50, this, &ShellBridge::updateTelemetry);
    } else {
        m_telemetryTimer.stop();
        ::malloc_trim(0);
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

void ShellBridge::setSystemVolume(int vol) {
    const int clamped = std::clamp(vol, 0, 100);
    const QString cmd = QStringLiteral("wpctl set-volume @DEFAULT_AUDIO_SINK@ %1%").arg(clamped);
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

void ShellBridge::toggleSystemMute() {
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")});
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

void ShellBridge::applyColorscheme(const QString &name) {
    if (!name.isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {name});
    }
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

QString ShellBridge::listFolderFiles(const QString &folderName) const {
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

void ShellBridge::openPath(const QString &path) {
    if (path.isEmpty()) return;
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"))) {
        QDesktopServices::openUrl(QUrl(path));
        return;
    }

    const QFileInfo fi(path);
    if (fi.isDir()) {
        const QString folderName = fi.fileName().toLower();
        if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("dolphin"), {fi.absoluteFilePath()});
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
        activateDolphinWindow(folderName);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ShellBridge::openContainingFolder(const QString &path) {
    if (path.isEmpty()) return;
    const QFileInfo fi(path);
    const QString folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    const QString folderName = QDir(folder).dirName().toLower();

    if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
        if (!fi.isDir() && fi.exists()) {
            QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), fi.absoluteFilePath()});
        } else {
            QProcess::startDetached(QStringLiteral("dolphin"), {folder});
        }
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }

    // Unminimize and bring open Dolphin window to the front
    activateDolphinWindow(folderName);
}

void ShellBridge::activateDolphinWindow(const QString &folderHint) {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Match")
    );
    msg << QString(); // query all open windows

    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.arguments().isEmpty()) return;

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return;

    arg.beginArray();
    QString bestMatchId;
    QString fallbackDolphinId;
    const QString hintLow = folderHint.toLower().trimmed();

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

        const QString tLow = text.toLower();
        const QString iLow = iconName.toLower();

        const bool isDolphin = iLow.contains(QStringLiteral("dolphin")) ||
                               iLow.contains(QStringLiteral("system-file-manager")) ||
                               tLow.contains(QStringLiteral("dolphin"));

        if (isDolphin) {
            if (fallbackDolphinId.isEmpty()) {
                fallbackDolphinId = id;
            }
            if (!hintLow.isEmpty() && tLow.contains(hintLow)) {
                bestMatchId = id;
                break;
            }
        }
    }
    arg.endArray();

    const QString targetId = !bestMatchId.isEmpty() ? bestMatchId : fallbackDolphinId;
    if (!targetId.isEmpty()) {
        auto runMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/WindowsRunner"),
            QStringLiteral("org.kde.krunner1"),
            QStringLiteral("Run")
        );
        runMsg << targetId << QString();
        QDBusConnection::sessionBus().call(runMsg);
    }
}

bool ShellBridge::moveToTrash(const QString &path) {
    if (path.isEmpty()) return false;
    return QFile::moveToTrash(path);
}

QString ShellBridge::searchFiles(const QString &pattern) const {
    const QString pat = pattern.trimmed();
    if (pat.isEmpty()) return QStringLiteral("[]");

    const QString homeDir = QDir::homePath();
    QProcess proc;
    QStringList args = {
        QStringLiteral("-i"), QStringLiteral("-a"),
        QStringLiteral("--max-results"), QStringLiteral("35"),
        QStringLiteral("--exclude"), QStringLiteral(".git"),
        QStringLiteral("--exclude"), QStringLiteral("node_modules"),
        QStringLiteral("--exclude"), QStringLiteral(".cache"),
        QStringLiteral("--exclude"), QStringLiteral(".local/share/Trash"),
        pat,
        homeDir
    };

    proc.start(QStringLiteral("fd"), args);
    if (!proc.waitForFinished(1500)) {
        proc.kill();
        return QStringLiteral("[]");
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QJsonArray items;
    for (const QString &rawPath : lines) {
        const QString fullPath = rawPath.trimmed();
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

// ── 5. Application Launching & Execution ───────────────────

QString ShellBridge::listAppsCategory(const QString &cat) const {
    return m_appIndexer ? m_appIndexer->listAppsCategoryJson(cat) : QStringLiteral("[]");
}

QString ShellBridge::getAppCategoryCounts() const {
    return m_appIndexer ? m_appIndexer->getAppCategoryCountsJson() : QStringLiteral("{}");
}

void ShellBridge::launchApp(const QString &desktop) {
    if (m_appIndexer) {
        m_appIndexer->launchApp(desktop);
        return;
    }

    if (desktop.trimmed().isEmpty()) return;

    QString cmd = desktop;
    cmd.remove(QRegularExpression(QStringLiteral("%[uUfFdiDnvm]")));
    cmd = cmd.trimmed();
    if (cmd.isEmpty()) return;

    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

QString ShellBridge::getAppActions(const QString &desktop) const {
    return m_appIndexer ? m_appIndexer->getAppActionsJson(desktop) : QStringLiteral("[]");
}

QString ShellBridge::searchWindows(const QString &query) const {
    const QString q = query.trimmed();
    if (q.isEmpty()) return QStringLiteral("[]");

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Match")
    );
    msg << q;

    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.arguments().isEmpty()) return QStringLiteral("[]");

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return QStringLiteral("[]");

    QJsonArray items;
    arg.beginArray();
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

        QJsonObject obj;
        obj[QStringLiteral("id")] = id;
        obj[QStringLiteral("title")] = text;
        obj[QStringLiteral("iconName")] = iconName;
        items.append(obj);
    }
    arg.endArray();

    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
}

bool ShellBridge::activateWindow(const QString &matchId) {
    if (matchId.isEmpty()) return false;

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Run")
    );
    msg << matchId << QString();
    auto reply = QDBusConnection::sessionBus().call(msg);
    return reply.type() != QDBusMessage::ErrorMessage;
}

void ShellBridge::editDesktopFile(const QString &desktop) {
    if (desktop.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("xdg-open"), {desktop});
}

void ShellBridge::openInDiscover(const QString &desktop) {
    if (desktop.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("plasma-discover"), {QStringLiteral("--app"), desktop});
}

void ShellBridge::openInTerminal(const QString &path) {
    const QString targetDir = path.isEmpty() ? QDir::homePath() : path;
    runInTerminal(QStringLiteral("cd \"") + targetDir + QStringLiteral("\" && exec $SHELL"));
}

void ShellBridge::runInTerminal(const QString &desktopOrCmd) {
    if (desktopOrCmd.trimmed().isEmpty()) return;

    QString term = QString::fromUtf8(qgetenv("TERMINAL"));
    if (term.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("konsole"),
            QStringLiteral("kitty"),
            QStringLiteral("alacritty"),
            QStringLiteral("foot"),
            QStringLiteral("wezterm"),
            QStringLiteral("xterm")
        };
        for (const auto &c : candidates) {
            if (!QStandardPaths::findExecutable(c).isEmpty()) {
                term = c;
                break;
            }
        }
    }
    if (term.isEmpty()) term = QStringLiteral("xterm");

    QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), desktopOrCmd});
}

void ShellBridge::runDetached(const QString &command) {
    if (command.trimmed().isEmpty()) return;
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void ShellBridge::executeCommand(const QString &cmdStr, bool inTerminal) {
    if (cmdStr.trimmed().isEmpty()) return;
    if (inTerminal) {
        runInTerminal(cmdStr);
    } else {
        runDetached(cmdStr);
    }
}

QString ShellBridge::bash(const QString &script) {
    if (script.trimmed().isEmpty()) return QString();

    QProcess proc;
    proc.start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), script});
    if (proc.waitForFinished(1500)) {
        return QString::fromUtf8(proc.readAllStandardOutput());
    }
    proc.kill();
    return QString();
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

QString ShellBridge::detectGpuVendor() const {
    QDir driDir(QStringLiteral("/sys/class/drm"));
    const QStringList cards = driDir.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &c : cards) {
        if (c.contains(QLatin1Char('-'))) continue;
        QFile vendorFile(QStringLiteral("/sys/class/drm/%1/device/vendor").arg(c));
        if (vendorFile.open(QIODevice::ReadOnly)) {
            const QString v = QString::fromUtf8(vendorFile.readAll()).trimmed().toLower();
            if (v == QStringLiteral("0x1002")) return QStringLiteral("amd");
            if (v == QStringLiteral("0x10de")) return QStringLiteral("nvidia");
            if (v == QStringLiteral("0x8086")) return QStringLiteral("intel");
        }
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty()) {
        return QStringLiteral("nvidia");
    }
    return QStringLiteral("amd");
}

QString ShellBridge::getNashiPath() const {
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("nashi"));
    if (!inPath.isEmpty()) return inPath;

    const QString localBin = QDir::homePath() + QStringLiteral("/.local/bin/nashi");
    if (QFile::exists(localBin)) return localBin;

    const QString localShare = QDir::homePath() + QStringLiteral("/.local/share/nashi/nashi");
    if (QFile::exists(localShare)) return localShare;

    const QString desktopPath = QStringLiteral("/home/dokuro/Masaüstü/orkunprogram/nashi-linux/nashi");
    if (QFile::exists(desktopPath)) return desktopPath;

    return QString();
}

void ShellBridge::installPackage(const QString &packageName, const QString &customCommand) {
    if (packageName == QStringLiteral("nashi") || packageName == QStringLiteral("nashi-app") || customCommand == QStringLiteral("install-nashi")) {
        installNashi();
        return;
    }

    QString cmd = customCommand.trimmed();
    if (cmd.isEmpty()) {
        cmd = QStringLiteral("sudo pacman -S --noconfirm %1 || yay -S --noconfirm %1").arg(packageName);
    }

    const QString script = QStringLiteral("%1; echo; echo 'Kurulum tamamlandı. Kapatmak için Enter tuşuna basın...'; read -r _").arg(cmd);
    runInTerminal(script);
}

void ShellBridge::installNashi() {
    const QString script = QStringLiteral(
        "echo '=== Nashi Kurulumu Başlatılıyor (GitHub: cdokuro/nashi-app) ==='; "
        "mkdir -p ~/.local/share && cd ~/.local/share && "
        "if [ -d nashi/.git ]; then "
        "  echo 'Mevcut kopya güncelleniyor...'; cd nashi && git pull; "
        "else "
        "  echo 'GitHub deposu klonlanıyor...'; rm -rf nashi && git clone https://github.com/cdokuro/nashi-app.git nashi && cd nashi; "
        "fi && "
        "chmod +x nashi && "
        "mkdir -p ~/.local/bin && ln -sf ~/.local/share/nashi/nashi ~/.local/bin/nashi && "
        "echo 'Nashi başarıyla kuruldu (~/.local/bin/nashi)!'; "
        "notify-send 'Nashi' 'Kurulum tamamlandı ve menüye bağlandı!' 2>/dev/null || true; "
        "echo; echo 'Devam etmek için Enter tuşuna basın...'; read -r _"
    );
    runInTerminal(script);
}

// ── 6. Power & System Actions ──────────────────────────────

void ShellBridge::powerOff() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logoutAndShutdown")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    }
}

void ShellBridge::rebootSystem() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logoutAndReboot")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    }
}

void ShellBridge::lockScreen() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("Lock")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    }
}

void ShellBridge::suspendSystem() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.PowerManagement"),
        QStringLiteral("/org/freedesktop/PowerManagement"),
        QStringLiteral("org.freedesktop.PowerManagement"),
        QStringLiteral("Suspend")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
    }
}

void ShellBridge::logoutUser() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logout")
    );
    msg << 1 << 2 << 2; // prompt, confirmation, etc.
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), QStringLiteral("self")});
    }
}

void ShellBridge::restartPlasma() {
    QProcess::startDetached(QStringLiteral("/bin/sh"), {
        QStringLiteral("-c"),
        QStringLiteral("systemctl --user restart plasma-plasmashell.service || (kquitapp6 plasmashell || killall plasmashell; kstart plasmashell)")
    });
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

bool ShellBridge::setPowerProfile(const QString &profile) {
    if (profile != QStringLiteral("power-saver") &&
        profile != QStringLiteral("balanced") &&
        profile != QStringLiteral("performance")) {
        return false;
    }

    m_powerProfile = profile;

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("net.hadess.PowerProfiles"),
        QStringLiteral("/net/hadess/PowerProfiles"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Set")
    );
    msg << QStringLiteral("net.hadess.PowerProfiles")
        << QStringLiteral("ActiveProfile")
        << QVariant::fromValue(QDBusVariant(profile));
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("powerprofilesctl"), {QStringLiteral("set"), profile});
    }

    emit powerProfileChanged(m_powerProfile);
    return true;
}

bool ShellBridge::isSleepInhibited() const {
    return (m_sleepInhibitCookie != 0) || m_sleepInhibited;
}

bool ShellBridge::setSleepInhibited(bool inhibit) {
    if (inhibit) {
        if (m_sleepInhibitCookie != 0) return true;

        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("Inhibit")
        );
        msg << QStringLiteral("RavenSpot") << QStringLiteral("Manually Block Sleep and Screen Locking");
        QDBusReply<uint> reply = QDBusConnection::sessionBus().call(msg);
        if (reply.isValid() && reply.value() != 0) {
            m_sleepInhibitCookie = reply.value();
            m_sleepInhibited = true;
            emit sleepInhibitedChanged(true);
            return true;
        }
        return false;
    } else {
        if (m_sleepInhibitCookie == 0) return true;

        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("UnInhibit")
        );
        msg << m_sleepInhibitCookie;
        QDBusConnection::sessionBus().call(msg);
        m_sleepInhibitCookie = 0;
        m_sleepInhibited = false;
        emit sleepInhibitedChanged(false);
        return true;
    }
}

// ── 7. Clipboard & Process Management ──────────────────────

QString ShellBridge::listClipboard() const {
    return QStringLiteral("[]");
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

QString ShellBridge::listProcesses(const QString &query) const {
    Q_UNUSED(query)
    return QStringLiteral("[]");
}

bool ShellBridge::killProcess(const QString &pid) {
    if (pid.trimmed().isEmpty()) return false;
    bool ok = false;
    int p = pid.toInt(&ok);
    if (!ok || p <= 1) return false;
    QProcess::startDetached(QStringLiteral("kill"), {QStringLiteral("-15"), QString::number(p)});
    return true;
}

// ── 8. Telemetry & Updates ─────────────────────────────────

QVariantMap ShellBridge::getSysMonitorInfo() const {
    QVariantMap map;
    map[QStringLiteral("ram_text")] = QStringLiteral("--");
    map[QStringLiteral("ram_pct")] = 0;
    map[QStringLiteral("disk_text")] = QStringLiteral("--");
    map[QStringLiteral("disk_pct")] = 0;
    map[QStringLiteral("temp")] = QStringLiteral("--");
    map[QStringLiteral("uptime")] = QStringLiteral("--");

    // RAM calculation via /proc/meminfo
    QFile memFile(QStringLiteral("/proc/meminfo"));
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        unsigned long long totalMem = 0;
        unsigned long long availMem = 0;
        while (!memFile.atEnd()) {
            const QByteArray line = memFile.readLine();
            if (line.startsWith("MemTotal:")) {
                const auto parts = line.simplified().split(' ');
                if (parts.size() >= 2) totalMem = parts[1].toULongLong();
            } else if (line.startsWith("MemAvailable:")) {
                const auto parts = line.simplified().split(' ');
                if (parts.size() >= 2) availMem = parts[1].toULongLong();
            }
        }
        memFile.close();
        if (totalMem > 0) {
            const unsigned long long usedMem = totalMem > availMem ? totalMem - availMem : 0;
            const int ramPct = static_cast<int>((usedMem * 100) / totalMem);
            map[QStringLiteral("ram_pct")] = ramPct;
            map[QStringLiteral("ram_text")] = QStringLiteral("%1 / %2 GB")
                .arg(usedMem / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(totalMem / (1024.0 * 1024.0), 0, 'f', 1);
        }
    }

    // Disk calculation via statvfs on "/"
    struct statvfs svfs{};
    if (statvfs("/", &svfs) == 0) {
        const unsigned long long total = svfs.f_blocks * svfs.f_frsize;
        const unsigned long long free = svfs.f_bavail * svfs.f_frsize;
        if (total > 0) {
            const unsigned long long used = total - free;
            const int diskPct = static_cast<int>((used * 100) / total);
            map[QStringLiteral("disk_pct")] = diskPct;
            map[QStringLiteral("disk_text")] = QStringLiteral("%1 / %2 GB")
                .arg(used / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0)
                .arg(total / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0);
        }
    }

    // Thermal read
    QFile tempFile(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const int milliC = tempFile.readLine().trimmed().toInt();
        tempFile.close();
        if (milliC > 0) {
            map[QStringLiteral("temp")] = QStringLiteral("%1°C").arg(milliC / 1000);
        }
    }

    // Uptime read
    QFile uptimeFile(QStringLiteral("/proc/uptime"));
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const double secs = QString::fromUtf8(uptimeFile.readLine()).section(' ', 0, 0).toDouble();
        uptimeFile.close();
        const int hours = static_cast<int>(secs) / 3600;
        const int mins = (static_cast<int>(secs) % 3600) / 60;
        map[QStringLiteral("uptime")] = QStringLiteral("%1h %2m").arg(hours).arg(mins);
    }

    return map;
}

QVariantMap ShellBridge::getUpdateInfo() const {
    const QString stateDir = QDir::homePath() + QStringLiteral("/.local/state/arch-update");
    const QString trayFile = stateDir + QStringLiteral("/tray_icon");
    const QString updatesFile = stateDir + QStringLiteral("/last_updates_check");
    const QString notifFile = QStringLiteral("/run/user/%1/arch-update/notif_param").arg(getuid());

    bool hasUpdates = false;
    int count = 0;
    QString trayVal;

    QFile fTray(trayFile);
    if (fTray.open(QIODevice::ReadOnly | QIODevice::Text)) {
        trayVal = QString::fromUtf8(fTray.readAll()).trimmed();
        fTray.close();
        if (trayVal.contains(QStringLiteral("updates-available"))) {
            hasUpdates = true;
        }
    }

    QFile fUpdates(updatesFile);
    if (fUpdates.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!fUpdates.atEnd()) {
            if (!fUpdates.readLine().trimmed().isEmpty()) {
                ++count;
            }
        }
        fUpdates.close();
        if (count > 0) {
            hasUpdates = true;
        }
    } else if (hasUpdates) {
        QFile fNotif(notifFile);
        if (fNotif.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString c = QString::fromUtf8(fNotif.readAll()).trimmed();
            fNotif.close();
            bool ok = false;
            int n = c.toInt(&ok);
            if (ok && n > 0) count = n;
        }
    }

    QString iconPath;
    QString tooltip;
    if (hasUpdates) {
        QString iconName = trayVal.contains(QStringLiteral("updates-available")) ? trayVal : QStringLiteral("cachy-update_updates-available-blue");
        iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/%1.svg").arg(iconName);
        if (!QFile::exists(iconPath)) {
            iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/cachy-update_updates-available-blue.svg");
        }
        QString cntStr = count > 0 ? QStringLiteral(" (%1)").arg(count) : QString();
        tooltip = QStringLiteral("Updates Available%1 — Click to Update").arg(cntStr);
    } else {
        QString iconName = (!trayVal.isEmpty() && !trayVal.contains(QStringLiteral("updates-available"))) ? trayVal : QStringLiteral("cachy-update-blue");
        iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/%1.svg").arg(iconName);
        if (!QFile::exists(iconPath)) {
            iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/cachy-update-blue.svg");
        }
        tooltip = QStringLiteral("System Up to Date (CachyOS)");
    }

    QVariantMap map;
    map[QStringLiteral("has_updates")] = hasUpdates;
    map[QStringLiteral("count")] = count;
    map[QStringLiteral("icon_path")] = iconPath;
    map[QStringLiteral("tooltip")] = tooltip;
    return map;
}

void ShellBridge::launchUpdate() {
    runInTerminal(QStringLiteral("cachy-update || yay -Syu || sudo pacman -Syu"));
}

void ShellBridge::openSystemMonitor(const QString &section) {
    if (section == QStringLiteral("gpu")) {
        if (!QStandardPaths::findExecutable(QStringLiteral("lact")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("lact"), {});
            return;
        }
    } else if (section == QStringLiteral("cpu")) {
        if (!QStandardPaths::findExecutable(QStringLiteral("cpu-x")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("cpu-x"), {});
            return;
        }
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("plasma-systemmonitor")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-systemmonitor"), {});
    } else if (!QStandardPaths::findExecutable(QStringLiteral("btop")).isEmpty()) {
        runInTerminal(QStringLiteral("btop"));
    } else if (!QStandardPaths::findExecutable(QStringLiteral("htop")).isEmpty()) {
        runInTerminal(QStringLiteral("htop"));
    }
}

void ShellBridge::updateTelemetry() {
    // Read /proc/stat for CPU
    QFile statFile(QStringLiteral("/proc/stat"));
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray firstLine = statFile.readLine();
        statFile.close();

        unsigned long long user = 0, nice = 0, sys = 0, idle = 0;
        if (sscanf(firstLine.constData(), "cpu %llu %llu %llu %llu", &user, &nice, &sys, &idle) == 4) {
            if (m_lastTotalUser != 0 || m_lastTotalSys != 0 || m_lastTotalIdle != 0) {
                const unsigned long long diffUser = (user > m_lastTotalUser) ? (user - m_lastTotalUser) : 0;
                const unsigned long long diffNice = (nice > m_lastTotalUserLow) ? (nice - m_lastTotalUserLow) : 0;
                const unsigned long long diffSys = (sys > m_lastTotalSys) ? (sys - m_lastTotalSys) : 0;
                const unsigned long long diffIdle = (idle > m_lastTotalIdle) ? (idle - m_lastTotalIdle) : 0;
                const unsigned long long totalDiff = diffUser + diffNice + diffSys + diffIdle;
                if (totalDiff > 0) {
                    m_lastCpuPercent = static_cast<int>(((diffUser + diffNice + diffSys) * 100) / totalDiff);
                }
            }
            m_lastTotalUser = user;
            m_lastTotalUserLow = nice;
            m_lastTotalSys = sys;
            m_lastTotalIdle = idle;
        }
    }

    // Try reading AMD/Intel GPU busy percent from sysfs
    QFile gpuFile(QStringLiteral("/sys/class/drm/card0/device/gpu_busy_percent"));
    if (gpuFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastGpuPercent = gpuFile.readLine().trimmed().toInt();
        gpuFile.close();
    }

    const int vol = m_audioEngine ? m_audioEngine->volume() : 10;
    const bool muted = m_audioEngine ? m_audioEngine->isMuted() : false;

    emit statsUpdated(m_lastCpuPercent, m_lastGpuPercent, vol, muted);

    QVariantMap metrics = getSysMonitorInfo();
    metrics[QStringLiteral("cpu")] = m_lastCpuPercent;
    metrics[QStringLiteral("gpu")] = m_lastGpuPercent;
    emit telemetryUpdated(metrics);
}

} // namespace ravenspot
