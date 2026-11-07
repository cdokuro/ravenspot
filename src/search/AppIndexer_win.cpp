// Windows half of AppIndexer: Start Menu shortcuts (.lnk) and packaged Store
// apps instead of XDG .desktop files; launching through the shell.
#include "AppIndexer.hpp"
#include "core/WinUtil.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <windows.h>
#include <shlobj.h>
#include <psapi.h>

namespace ravenspot {

namespace {

bool containsAny(const QString &haystack, std::initializer_list<const char *> needles) {
    for (const char *n : needles) {
        if (haystack.contains(QLatin1String(n), Qt::CaseInsensitive)) return true;
    }
    return false;
}

QString exeOfWindowProcess(HWND hwnd) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return QString();
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return QString();
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = MAX_PATH * 2;
    QString path;
    if (::QueryFullProcessImageNameW(h, 0, buf, &len)) path = QDir::fromNativeSeparators(QString::fromWCharArray(buf, len));
    ::CloseHandle(h);
    return path;
}

struct FindCtx { QString exe; HWND found{nullptr}; };

BOOL CALLBACK findWindowForExe(HWND hwnd, LPARAM lp) {
    auto *ctx = reinterpret_cast<FindCtx *>(lp);
    if (!::IsWindowVisible(hwnd) || ::GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (::GetWindowTextLengthW(hwnd) == 0) return TRUE;
    const LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (exeOfWindowProcess(hwnd).compare(ctx->exe, Qt::CaseInsensitive) == 0) {
        ctx->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

} // namespace

QString AppIndexer::categorizeShortcut(const QString &relativeFolder, const QString &name, const QString &target) {
    const QString all = relativeFolder + QLatin1Char('|') + name + QLatin1Char('|') + QFileInfo(target).fileName();
    if (containsAny(all, {"game", "steam", "epic games", "gog galaxy", "ubisoft", "battle.net", "riot", "xbox", "minecraft"}))
        return QStringLiteral("games");
    if (containsAny(all, {"visual studio", "vs code", "vscode", "code.exe", "git", "python", "node.js", "jetbrains",
                          "intellij", "pycharm", "android studio", "docker", "developer", "sdk", "cmake", "qt creator", "wsl"}))
        return QStringLiteral("development");
    if (containsAny(all, {"chrome", "firefox", "edge", "opera", "brave", "vivaldi", "discord", "telegram", "whatsapp",
                          "zoom", "teams", "slack", "thunderbird", "outlook", "skype", "qbittorrent", "filezilla", "signal"}))
        return QStringLiteral("internet");
    if (containsAny(all, {"word", "excel", "powerpoint", "onenote", "office", "libreoffice", "acrobat", "pdf",
                          "notion", "obsidian", "calendar", "mail"}))
        return QStringLiteral("office");
    if (containsAny(all, {"vlc", "spotify", "media", "music", "video", "audio", "obs", "audacity", "itunes",
                          "mpv", "potplayer", "movies", "photos"}))
        return QStringLiteral("multimedia");
    if (containsAny(all, {"photoshop", "gimp", "paint", "krita", "blender", "inkscape", "illustrator", "figma", "snipping"}))
        return QStringLiteral("graphics");
    if (containsAny(all, {"administrative tools", "windows tools", "system tools", "powershell", "command prompt",
                          "control panel", "task manager", "terminal", "registry", "device manager", "settings"}))
        return QStringLiteral("system");
    return QStringLiteral("utilities");
}

bool AppIndexer::parseShortcutFile(const QString &filePath, AppEntry &outEntry) {
    const QFileInfo fi(filePath);
    const QString name = fi.completeBaseName();
    if (name.isEmpty()) return false;
    // Uninstallers, readmes and web links clutter the launcher
    static const QRegularExpression junk(QStringLiteral(
        "(uninstall|kaldır|readme|read me|release notes|documentation|help|website|license|manual)"),
        QRegularExpression::CaseInsensitiveOption);
    if (junk.match(name).hasMatch()) return false;

    QString target, args, workDir;
    win::resolveShortcut(filePath, &target, &args, &workDir);
    if (!target.isEmpty()) {
        const QString ext = QFileInfo(target).suffix().toLower();
        static const QStringList docExt = {QStringLiteral("chm"), QStringLiteral("txt"), QStringLiteral("pdf"),
                                           QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("url"),
                                           QStringLiteral("rtf"), QStringLiteral("md")};
        if (docExt.contains(ext)) return false;
    }

    // Folder below "Start Menu/Programs" (e.g. "Accessories", "Steam")
    QString rel;
    const int idx = filePath.indexOf(QStringLiteral("/Start Menu/Programs/"), 0, Qt::CaseInsensitive);
    if (idx >= 0) rel = QFileInfo(filePath.mid(idx + 21)).path();
    if (rel == QLatin1String(".")) rel.clear();

    outEntry = AppEntry{};
    outEntry.id = name;
    outEntry.label = name;
    outEntry.labelEn = name;
    outEntry.labelTr = name;
    outEntry.genericName = rel;
    outEntry.comment = target.isEmpty() ? rel : QDir::toNativeSeparators(target);
    outEntry.icon = filePath;           // AppIconProvider asks the shell for the .lnk icon
    outEntry.desktopPath = filePath;    // launchApp() opens the shortcut itself
    outEntry.rawExec = target.isEmpty() ? filePath : target + (args.isEmpty() ? QString() : QLatin1Char(' ') + args);
    outEntry.cleanedExec = outEntry.rawExec;
    outEntry.category = categorizeShortcut(rel, name, target);
    outEntry.workingDir = workDir;
    const QString exeBase = QFileInfo(target).completeBaseName();
    if (!exeBase.isEmpty() && exeBase.compare(name, Qt::CaseInsensitive) != 0) outEntry.aliases << exeBase;
    if (!rel.isEmpty()) outEntry.aliases << QFileInfo(rel).fileName();
    for (const QString &t : name.split(QRegularExpression(QStringLiteral("[\\s\\-_.]+")), Qt::SkipEmptyParts))
        outEntry.searchTerms << t.toLower();
    for (const QString &a : outEntry.aliases) outEntry.searchTerms << a.toLower();
    return true;
}

void AppIndexer::loadPackagedAppsAsync() {
    if (m_packagedLoading) return;
    m_packagedLoading = true;
    auto *proc = new QProcess(this);
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_packagedLoading = false;
        proc->deleteLater();
        if (code != 0) return;
        const QJsonDocument doc = QJsonDocument::fromJson(proc->readAllStandardOutput());
        const QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray{doc.object()};
        std::vector<AppEntry> apps;
        for (const QJsonValue &v : arr) {
            const QString name = v.toObject().value(QStringLiteral("Name")).toString();
            const QString appId = v.toObject().value(QStringLiteral("AppID")).toString();
            // Packaged apps have "Family!App" ids; desktop apps are already covered by .lnk files
            if (name.isEmpty() || !appId.contains(QLatin1Char('!'))) continue;
            AppEntry e;
            e.id = appId;
            e.label = e.labelEn = e.labelTr = name;
            e.icon = QStringLiteral("uwp:") + appId;
            e.desktopPath = QStringLiteral("shell:AppsFolder\\") + appId;
            e.rawExec = e.cleanedExec = e.desktopPath;
            e.category = categorizeShortcut(QString(), name, QString());
            e.comment = QStringLiteral("Microsoft Store app");
            e.searchTerms << name.toLower();
            apps.push_back(std::move(e));
        }
        m_packagedApps = std::move(apps);
        rebuildIndex();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        m_packagedLoading = false;
        proc->deleteLater();
    });
    proc->start(QStringLiteral("powershell.exe"),
                {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                 QStringLiteral("-Command"),
                 QStringLiteral("[Console]::OutputEncoding=[Text.Encoding]::UTF8; Get-StartApps | Select-Object Name,AppID | ConvertTo-Json -Compress")});
}

bool AppIndexer::activateExistingWindow(const QString &desktopOrExec) const {
    // Focus a running instance instead of starting a second one (Linux does the same via KWin)
    QString exe;
    auto snapshot = m_snapshot.load();
    if (snapshot) {
        auto it = snapshot->desktopPathLookup.find(desktopOrExec.trimmed());
        if (it != snapshot->desktopPathLookup.end()) exe = snapshot->allApps[it->second].rawExec;
    }
    if (exe.startsWith(QLatin1String("shell:")) || exe.isEmpty()) return false;
    // rawExec = "<target> <args>": only shortcuts without arguments are matched
    if (!QFileInfo(exe).isFile() || !exe.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) return false;
    FindCtx ctx{exe, nullptr};
    ::EnumWindows(findWindowForExe, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found) return false;
    if (::IsIconic(ctx.found)) ::ShowWindow(ctx.found, SW_RESTORE);
    ::AllowSetForegroundWindow(ASFW_ANY);
    ::SetForegroundWindow(ctx.found);
    return true;
}

bool AppIndexer::activateTrayApp(const QString &desktopOrExec) const {
    Q_UNUSED(desktopOrExec)
    return false; // tray windows are hidden; the app's own single-instance logic handles a re-launch
}

bool AppIndexer::activateTrayOrWindow(const QString &desktopOrExec) const {
    return activateExistingWindow(desktopOrExec);
}

void AppIndexer::launchApp(const QString &desktopOrExec) {
    const QString trimmed = desktopOrExec.trimmed();
    if (trimmed.isEmpty()) return;
    if (activateExistingWindow(trimmed)) return;

    if (trimmed.startsWith(QLatin1String("shell:AppsFolder\\"), Qt::CaseInsensitive)) {
        QProcess::startDetached(QStringLiteral("explorer.exe"), {trimmed});
        return;
    }
    if (QFileInfo::exists(trimmed)) {
        // The shell resolves .lnk targets, arguments, working dir and "run as admin" flags
        if (!win::shellExecute(trimmed)) QDesktopServices::openUrl(QUrl::fromLocalFile(trimmed));
        return;
    }
    // Plain command ("notepad", "code .") as typed in a menu entry
    const QString cleaned = cleanExecCommand(trimmed);
    if (cleaned.isEmpty()) return;
    QStringList parts = QProcess::splitCommand(cleaned);
    if (parts.isEmpty()) return;
    const QString program = parts.takeFirst();
    if (!QProcess::startDetached(program, parts, QDir::homePath())) {
        win::shellExecute(program, parts.join(QLatin1Char(' ')));
    }
}

void AppIndexer::openContainingFolder(const QString &desktopPath) const {
    if (desktopPath.isEmpty() || desktopPath.startsWith(QLatin1String("shell:"))) return;
    win::revealInExplorer(desktopPath);
}

void AppIndexer::editDesktopFile(const QString &desktopPath) const {
    // Shortcut properties dialog (target, arguments, icon, "run as administrator")
    if (desktopPath.isEmpty() || !QFileInfo::exists(desktopPath)) return;
    win::shellExecute(desktopPath, QString(), QStringLiteral("properties"));
}

void AppIndexer::openInDiscover(const QString &desktopPath) const {
    Q_UNUSED(desktopPath)
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:appsfeatures")));
}

void AppIndexer::runInTerminal(const QString &desktopOrCmd) const {
    const QString cmd = cleanExecCommand(desktopOrCmd);
    if (cmd.isEmpty()) return;
    win::openTerminal(cmd, QDir::homePath());
}

} // namespace ravenspot
