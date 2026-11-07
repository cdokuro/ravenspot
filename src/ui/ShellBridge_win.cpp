// Windows ShellBridge methods: the counterparts of ShellBridge_linux.cpp.
// D-Bus/KDE/procfs calls become Win32 (power, audio endpoint, ToolHelp32,
// EnumWindows), Konsole/Dolphin become Windows Terminal/PowerShell/Explorer.
// Every method keeps the JSON shape Menu.qml already expects.
#include "ShellBridge.hpp"
#include "ThemeManager.hpp"
#include "../audio/AudioEngine.hpp"
#include "../core/LayerShellWindow.hpp"
#include "../core/WinUtil.hpp"
#include "../search/AppIndexer.hpp"
#include "../run/CommandAnalyzer.hpp"
#include "../run/CommandRunner.hpp"
#include "../run/CommandHistory.hpp"
#include "pc/ClipboardHistory.hpp"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <powrprof.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace ravenspot {

namespace platform {
void trimHeap() {
    // Rough analogue of malloc_trim: hand the idle working set back to the OS
    ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}
} // namespace platform

namespace {

QString toJson(const QJsonArray &arr) {
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ── Default playback device (Core Audio) ───────────────────────
template <typename Fn>
bool withEndpointVolume(Fn fn) {
    const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IMMDeviceEnumerator *enumerator = nullptr;
    if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator)))) {
        IMMDevice *device = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            IAudioEndpointVolume *vol = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void **>(&vol)))) {
                ok = fn(vol);
                vol->Release();
            }
            device->Release();
        }
        enumerator->Release();
    }
    if (SUCCEEDED(init)) ::CoUninitialize();
    return ok;
}

// ── Power schemes (powercfg GUIDs) ─────────────────────────────
const GUID kSchemePowerSaver  = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
const GUID kSchemeBalanced    = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
const GUID kSchemePerformance = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

QString activePowerProfile() {
    GUID *active = nullptr;
    if (::PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) return QStringLiteral("balanced");
    QString p = QStringLiteral("balanced");
    if (IsEqualGUID(*active, kSchemePowerSaver)) p = QStringLiteral("power-saver");
    else if (IsEqualGUID(*active, kSchemePerformance)) p = QStringLiteral("performance");
    ::LocalFree(active);
    return p;
}

bool enablePrivilege(const wchar_t *name) {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = ::LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)
                    && ::AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr)
                    && ::GetLastError() == ERROR_SUCCESS;
    ::CloseHandle(token);
    return ok;
}

void exitWindows(UINT flags) {
    enablePrivilege(SE_SHUTDOWN_NAME);
    // Planned shutdown reason: "Other (Planned)"
    ::ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
}

// ── Windows (EnumWindows) ──────────────────────────────────────
QString processImagePath(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return QString();
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = MAX_PATH * 2;
    QString path;
    if (::QueryFullProcessImageNameW(h, 0, buf, &len)) path = QDir::fromNativeSeparators(QString::fromWCharArray(buf, len));
    ::CloseHandle(h);
    return path;
}

struct WindowRow { HWND hwnd; QString title; QString exe; };

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM lp) {
    auto *rows = reinterpret_cast<QList<WindowRow> *>(lp);
    if (!::IsWindowVisible(hwnd) || ::GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    const LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    const int len = ::GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    ::GetWindowTextW(hwnd, buf.data(), len + 1);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ::GetCurrentProcessId()) return TRUE;
    rows->append({hwnd, QString::fromWCharArray(buf.c_str()), processImagePath(pid)});
    return TRUE;
}

QString windowSearchJson(const QString &query) {
    const QString q = query.trimmed();
    if (q.isEmpty()) return QStringLiteral("[]");
    QList<WindowRow> rows;
    ::EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&rows));
    QJsonArray items;
    for (const WindowRow &r : rows) {
        const QString exeName = QFileInfo(r.exe).completeBaseName();
        if (!r.title.contains(q, Qt::CaseInsensitive) && !exeName.contains(q, Qt::CaseInsensitive)) continue;
        QJsonObject obj;
        obj[QStringLiteral("id")] = QString::number(reinterpret_cast<quintptr>(r.hwnd));
        obj[QStringLiteral("title")] = r.title;
        obj[QStringLiteral("iconName")] = r.exe; // appicon provider resolves .exe icons
        items.append(obj);
        if (items.size() >= 15) break;
    }
    return toJson(items);
}

// "xdg-open 'https://…'" from menu.jsonc / web search rows -> the URL or path
QString xdgOpenTarget(const QString &command) {
    static const QRegularExpression re(QStringLiteral("^\\s*xdg-open\\s+(?:'([^']*)'|\"([^\"]*)\"|(\\S+))\\s*$"));
    const auto m = re.match(command);
    if (!m.hasMatch()) return QString();
    for (int i = 1; i <= 3; ++i) {
        if (!m.captured(i).isEmpty()) return m.captured(i);
    }
    return QString();
}

QString startupFolder() {
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("APPDATA")))
           + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup");
}

QString fileTimeText(const QFileInfo &fi) {
    return fi.lastModified().toString(QStringLiteral("dd.MM.yyyy hh:mm"));
}

QString rssText(qint64 kb) {
    if (kb >= 1024 * 1024) return QStringLiteral("%1 GB").arg(QString::number(kb / 1048576.0, 'f', 1));
    if (kb >= 1024) return QStringLiteral("%1 MB").arg(QString::number(kb / 1024.0, 'f', 1));
    return QStringLiteral("%1 KB").arg(kb);
}

unsigned long long fileTimeToU64(const FILETIME &ft) {
    return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

// ── Construction ───────────────────────────────────────────────

void ShellBridge::initPlatform() {
    // No orkun-clipboard-daemon on Windows: record clipboard text ourselves
    // (memory only, like the Linux daemon's history).
    if (auto *cb = QGuiApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, [this, cb]() {
            const QString text = cb->text();
            if (text.trimmed().isEmpty()) return;
            ClipEntry e;
            e.text = text;
            e.fingerprint = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
            e.timestamp = QDateTime::currentMSecsSinceEpoch();
            m_clipHistory->ingest(e);
        });
    }
    connect(m_commandRunner, &CommandRunner::finished, this, [this](int code, bool crashed) {
        // Menu closed while an inline command finished: bring the result back
        // (Windows has no notify-send; reopening shows the output panel).
        if (m_layerShellWindow && m_layerShellWindow->isVisible()) return;
        Q_UNUSED(code)
        Q_UNUSED(crashed)
        ::MessageBeep(MB_ICONASTERISK);
    });
    m_powerProfile = activePowerProfile();
}

// ── Audio ──────────────────────────────────────────────────────

void ShellBridge::setSystemVolume(int vol) {
    const float level = static_cast<float>(std::clamp(vol, 0, 100)) / 100.0f;
    withEndpointVolume([level](IAudioEndpointVolume *v) {
        v->SetMute(FALSE, nullptr);
        return SUCCEEDED(v->SetMasterVolumeLevelScalar(level, nullptr));
    });
}

void ShellBridge::toggleSystemMute() {
    withEndpointVolume([](IAudioEndpointVolume *v) {
        BOOL muted = FALSE;
        if (FAILED(v->GetMute(&muted))) return false;
        return SUCCEEDED(v->SetMute(!muted, nullptr));
    });
}

void ShellBridge::applyColorscheme(const QString &name) {
    Q_UNUSED(name) // KDE colour schemes only; Windows follows RavenSpot's own themes
}

// ── Files & folders ────────────────────────────────────────────

void ShellBridge::openPath(const QString &rawPath) {
    const QString path = rawPath.startsWith(QLatin1String("~/")) || rawPath == QLatin1String("~")
        ? QDir::homePath() + rawPath.mid(1) : rawPath;
    if (path.isEmpty()) return;
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"))) {
        QDesktopServices::openUrl(QUrl(path));
        return;
    }
    if (!win::shellExecute(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ShellBridge::openContainingFolder(const QString &path) {
    if (path.isEmpty()) return;
    const QFileInfo fi(path);
    if (fi.isDir()) win::shellExecute(fi.absoluteFilePath());
    else win::revealInExplorer(path);
}

void ShellBridge::activateDolphinWindow(const QString &folderHint) {
    Q_UNUSED(folderHint) // Explorer raises its own window
}

void ShellBridge::launchApp(const QString &desktop) {
    if (m_appIndexer) {
        m_appIndexer->launchApp(desktop);
        return;
    }
    if (!desktop.trimmed().isEmpty()) win::shellExecute(desktop.trimmed());
}

// ── Windows ────────────────────────────────────────────────────

QString ShellBridge::searchWindows(const QString &query) const {
    return windowSearchJson(query);
}

void ShellBridge::requestWindowSearch(const QString &query, int requestId) {
    // EnumWindows over ~50 windows takes well under a millisecond: answer on the next tick
    QTimer::singleShot(0, this, [this, query, requestId]() {
        emit windowSearchFinished(requestId, query, windowSearchJson(query));
    });
}

bool ShellBridge::activateWindow(const QString &matchId) {
    bool ok = false;
    const quintptr raw = matchId.toULongLong(&ok);
    if (!ok || !raw) return false;
    HWND hwnd = reinterpret_cast<HWND>(raw);
    if (!::IsWindow(hwnd)) return false;
    if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
    ::AllowSetForegroundWindow(ASFW_ANY);
    return ::SetForegroundWindow(hwnd) != FALSE;
}

void ShellBridge::editDesktopFile(const QString &desktop) {
    if (m_appIndexer) m_appIndexer->editDesktopFile(desktop);
}

void ShellBridge::openInDiscover(const QString &desktop) {
    if (m_appIndexer) m_appIndexer->openInDiscover(desktop);
}

void ShellBridge::openInTerminal(const QString &path) {
    const QString targetDir = files::resolveTerminalDir(path, QDir::homePath());
    win::openTerminal(QString(), targetDir);
}

void ShellBridge::extractHere(const QString &path) {
    // bsdtar ships with Windows 10 1803+ and reads zip/7z/rar/tar.*
    const QFileInfo fi(path);
    QProcess p;
    p.setProgram(QStringLiteral("tar.exe"));
    p.setArguments({QStringLiteral("-xf"), QDir::toNativeSeparators(fi.absoluteFilePath()),
                    QStringLiteral("-C"), QDir::toNativeSeparators(fi.absolutePath())});
    p.setWorkingDirectory(fi.absolutePath());
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    p.startDetached();
}

void ShellBridge::compressPath(const QString &path) {
    // tar -a picks the format from the suffix: <name>.zip next to the item
    const QFileInfo fi(path);
    QProcess p;
    p.setProgram(QStringLiteral("tar.exe"));
    p.setArguments({QStringLiteral("-a"), QStringLiteral("-cf"), fi.fileName() + QStringLiteral(".zip"), fi.fileName()});
    p.setWorkingDirectory(fi.absolutePath());
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    p.startDetached();
}

QString ShellBridge::openWithApps(const QString &path) const {
    Q_UNUSED(path)
    // Windows keeps its handler list private; offer the system "Open with" dialog.
    QJsonArray arr;
    arr.append(QJsonObject{{QStringLiteral("file"), QStringLiteral("__openas__")},
                           {QStringLiteral("name"), QStringLiteral("Choose another app…")},
                           {QStringLiteral("icon"), QStringLiteral("system-run")},
                           {QStringLiteral("isDefault"), false}});
    return toJson(arr);
}

void ShellBridge::openWith(const QString &path, const QString &desktopFile) {
    if (path.isEmpty()) return;
    if (desktopFile == QLatin1String("__openas__") || desktopFile.isEmpty()) {
        const std::wstring w = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
        OPENASINFO info{};
        info.pcszFile = w.c_str();
        info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
        ::SHOpenWithDialog(nullptr, &info);
        return;
    }
    // An app shortcut/exe picked elsewhere: run it with the file as argument
    win::shellExecute(desktopFile, QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(path)));
}

// ── Apps: autostart / uninstall ────────────────────────────────

bool ShellBridge::isAutostart(const QString &desktopFile) const {
    if (desktopFile.isEmpty() || desktopFile.startsWith(QLatin1String("shell:"))) return false;
    return QFileInfo::exists(startupFolder() + QLatin1Char('/') + QFileInfo(desktopFile).fileName());
}

bool ShellBridge::setAutostart(const QString &desktopFile, bool enabled) {
    if (desktopFile.isEmpty() || desktopFile.startsWith(QLatin1String("shell:"))) return false;
    const QString dest = startupFolder() + QLatin1Char('/') + QFileInfo(desktopFile).fileName();
    if (!enabled) return !QFileInfo::exists(dest) || QFile::remove(dest);
    QDir().mkpath(startupFolder());
    if (QFileInfo::exists(dest)) return true;
    return QFile::copy(desktopFile, dest);
}

bool ShellBridge::canUninstall(const QString &desktopFile) const {
    return !desktopFile.isEmpty();
}

void ShellBridge::uninstallApp(const QString &desktopFile) {
    Q_UNUSED(desktopFile)
    // Uninstalling is the system's job: open "Installed apps"
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:appsfeatures")));
}

// ── Commands ───────────────────────────────────────────────────

void ShellBridge::runCommandInTerminal(const QString &command) {
    if (command.trimmed().isEmpty()) return;
    win::openTerminal(command, QDir::homePath());
    m_commandHistory->add(command);
}

void ShellBridge::runInTerminal(const QString &desktopOrCmd) {
    if (desktopOrCmd.trimmed().isEmpty()) return;
    win::openTerminal(desktopOrCmd, QDir::homePath());
}

void ShellBridge::runDetached(const QString &command) {
    if (command.trimmed().isEmpty()) return;
    // menu.jsonc and the web-search rows use "xdg-open '<url>'"
    const QString target = xdgOpenTarget(command);
    if (!target.isEmpty()) {
        openPath(target);
        return;
    }
    QProcess p;
    p.setProgram(QStringLiteral("powershell.exe"));
    p.setArguments({QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
                    QStringLiteral("Hidden"), QStringLiteral("-Command"), command});
    p.setWorkingDirectory(QDir::homePath());
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    p.startDetached();
}

QString ShellBridge::bash(const QString &script) {
    // Menu guard scripts are bash. With Git for Windows they run as-is (Linux-only
    // checks such as pacman simply report "missing"); without it no guards apply.
    if (script.trimmed().isEmpty()) return QString();
    const QString bashExe = win::gitBashPath();
    if (bashExe.isEmpty()) return QString();
    QProcess proc;
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    proc.start(bashExe, {QStringLiteral("-c"), script});
    if (proc.waitForFinished(1500)) return QString::fromUtf8(proc.readAllStandardOutput());
    proc.kill();
    return QString();
}

QString ShellBridge::detectGpuVendor() const {
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; ::EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        const QString id = QString::fromWCharArray(dd.DeviceID).toUpper();
        if (id.contains(QLatin1String("VEN_10DE"))) return QStringLiteral("nvidia");
        if (id.contains(QLatin1String("VEN_1002"))) return QStringLiteral("amd");
        if (id.contains(QLatin1String("VEN_8086"))) return QStringLiteral("intel");
        dd = DISPLAY_DEVICEW{};
        dd.cb = sizeof(dd);
    }
    return QStringLiteral("amd");
}

QString ShellBridge::getNashiPath() const {
    return QStandardPaths::findExecutable(QStringLiteral("nashi"));
}

void ShellBridge::installPackage(const QString &packageName, const QString &customCommand) {
    QString cmd = customCommand.trimmed();
    // Install hints in Menu.qml are written for Arch (pacman/yay); use winget instead
    static const QRegularExpression linuxPm(QStringLiteral("\\b(pacman|yay|paru|apt|apt-get|dnf|zypper|sudo)\\b"));
    if (cmd.isEmpty() || linuxPm.match(cmd).hasMatch()) {
        cmd = QStringLiteral("winget install \"%1\" --accept-source-agreements --accept-package-agreements")
                  .arg(QString(packageName).remove(QLatin1Char('"')));
    }
    runInTerminal(cmd + QStringLiteral("; Write-Host ''; Read-Host 'Done. Press Enter to close'; exit"));
}

void ShellBridge::installNashi() {
    runInTerminal(QStringLiteral("Write-Host 'Nashi is a Linux application and is not available on Windows.'; "
                                 "Read-Host 'Press Enter to close'; exit"));
}

// ── Power ──────────────────────────────────────────────────────

void ShellBridge::powerOff() { exitWindows(EWX_POWEROFF); }
void ShellBridge::rebootSystem() { exitWindows(EWX_REBOOT); }
void ShellBridge::lockScreen() { ::LockWorkStation(); }
void ShellBridge::suspendSystem() { ::SetSuspendState(FALSE, FALSE, FALSE); }
void ShellBridge::logoutUser() { ::ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED); }
void ShellBridge::saveSession() {} // no session save/restore on Windows

void ShellBridge::restartPlasma() {
    // Windows analogue of restarting the desktop shell: restart Explorer
    QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-Command"),
         QStringLiteral("Stop-Process -Name explorer -Force; Start-Sleep -Milliseconds 800; "
                        "if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer }")});
}

bool ShellBridge::setPowerProfile(const QString &profile) {
    const GUID *scheme = nullptr;
    if (profile == QLatin1String("power-saver")) scheme = &kSchemePowerSaver;
    else if (profile == QLatin1String("balanced")) scheme = &kSchemeBalanced;
    else if (profile == QLatin1String("performance")) scheme = &kSchemePerformance;
    if (!scheme) return false;
    // Modern Standby laptops often only have "Balanced": then nothing changes
    if (::PowerSetActiveScheme(nullptr, scheme) != ERROR_SUCCESS) return false;
    m_powerProfile = profile;
    emit powerProfileChanged(m_powerProfile);
    return true;
}

bool ShellBridge::setSleepInhibited(bool inhibit) {
    if (inhibit) {
        if (m_powerRequest) return true;
        REASON_CONTEXT ctx{};
        ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
        ctx.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        static wchar_t reason[] = L"RavenSpot: Manually Block Sleep and Screen Locking";
        ctx.Reason.SimpleReasonString = reason;
        HANDLE req = ::PowerCreateRequest(&ctx);
        if (req == INVALID_HANDLE_VALUE) return false;
        ::PowerSetRequest(req, PowerRequestSystemRequired);
        ::PowerSetRequest(req, PowerRequestDisplayRequired);
        m_powerRequest = req;
        m_sleepInhibitCookie = 1; // the destructor releases it, as on Linux
        m_sleepInhibited = true;
        emit sleepInhibitedChanged(true);
        return true;
    }
    if (!m_powerRequest) return true;
    HANDLE req = static_cast<HANDLE>(m_powerRequest);
    ::PowerClearRequest(req, PowerRequestSystemRequired);
    ::PowerClearRequest(req, PowerRequestDisplayRequired);
    ::CloseHandle(req);
    m_powerRequest = nullptr;
    m_sleepInhibitCookie = 0;
    m_sleepInhibited = false;
    emit sleepInhibitedChanged(false);
    return true;
}

// ── Recent files / processes ───────────────────────────────────

QString ShellBridge::recentFiles(const QString &query) const {
    // %APPDATA%\Microsoft\Windows\Recent holds one .lnk per recently opened item
    const QString recentDir = QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("APPDATA")))
                              + QStringLiteral("/Microsoft/Windows/Recent");
    const QFileInfoList links = QDir(recentDir).entryInfoList({QStringLiteral("*.lnk")}, QDir::Files, QDir::Time);
    const QString q = query.trimmed();
    QJsonArray arr;
    for (const QFileInfo &lnk : links) {
        if (arr.size() >= 20) break;
        if (!q.isEmpty() && !lnk.completeBaseName().contains(q, Qt::CaseInsensitive)) continue;
        QString target;
        if (!win::resolveShortcut(lnk.absoluteFilePath(), &target) || target.isEmpty()) continue;
        const QFileInfo fi(target);
        if (!fi.exists()) continue;
        arr.append(QJsonObject{{QStringLiteral("path"), fi.absoluteFilePath()},
                               {QStringLiteral("name"), fi.fileName()},
                               {QStringLiteral("isDir"), fi.isDir()},
                               {QStringLiteral("modified"), fileTimeText(lnk)}});
    }
    return toJson(arr);
}

QString ShellBridge::listProcesses(const QString &query) const {
    const QString q = query.trimmed();
    if (q.isEmpty()) return QStringLiteral("[]");
    struct Row { DWORD pid; QString name; QString cmd; qint64 rssKb; };
    QList<Row> rows;
    DWORD mySession = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &mySession);
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return QStringLiteral("[]");
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID <= 4) continue; // Idle / System
        DWORD session = 0;
        // Only this desktop session's processes (no services), like the Linux UID filter
        if (!::ProcessIdToSessionId(pe.th32ProcessID, &session) || session != mySession) continue;
        const QString name = QString::fromWCharArray(pe.szExeFile);
        const QString path = processImagePath(pe.th32ProcessID);
        if (!name.contains(q, Qt::CaseInsensitive) && !path.contains(q, Qt::CaseInsensitive)) continue;
        qint64 rss = 0;
        if (HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            PROCESS_MEMORY_COUNTERS pmc{};
            if (::GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) rss = static_cast<qint64>(pmc.WorkingSetSize / 1024);
            ::CloseHandle(h);
        }
        rows.push_back({pe.th32ProcessID, name, path.isEmpty() ? name : QDir::toNativeSeparators(path), rss});
    }
    ::CloseHandle(snap);
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.rssKb > b.rssKb; });
    QJsonArray arr;
    for (const Row &r : rows) {
        if (arr.size() >= 15) break;
        arr.append(QJsonObject{{QStringLiteral("pid"), static_cast<qint64>(r.pid)}, {QStringLiteral("name"), r.name},
                               {QStringLiteral("cmd"), r.cmd}, {QStringLiteral("rssKb"), r.rssKb},
                               {QStringLiteral("rssText"), rssText(r.rssKb)}});
    }
    return toJson(arr);
}

bool ShellBridge::forceKillProcess(const QString &pid) {
    bool ok = false;
    const qint64 p = pid.trimmed().toLongLong(&ok);
    if (!ok || p <= 4 || p == static_cast<qint64>(::GetCurrentProcessId())) return false;
    HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(p));
    if (!h) return false;
    const bool done = ::TerminateProcess(h, 1) != FALSE;
    ::CloseHandle(h);
    return done;
}

bool ShellBridge::killProcess(const QString &pid) {
    // Polite close first (WM_CLOSE to its windows via taskkill without /F);
    // the second Enter in Menu.qml calls forceKillProcess.
    bool ok = false;
    const qint64 p = pid.trimmed().toLongLong(&ok);
    if (!ok || p <= 4 || p == static_cast<qint64>(::GetCurrentProcessId())) return false;
    QProcess t;
    t.setProgram(QStringLiteral("taskkill.exe"));
    t.setArguments({QStringLiteral("/PID"), QString::number(p)});
    t.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    return t.startDetached();
}

// ── Telemetry ──────────────────────────────────────────────────

QVariantMap ShellBridge::getSysMonitorInfo() const {
    QVariantMap map;
    map[QStringLiteral("ram_text")] = QStringLiteral("--");
    map[QStringLiteral("ram_pct")] = 0;
    map[QStringLiteral("disk_text")] = QStringLiteral("--");
    map[QStringLiteral("disk_pct")] = 0;
    map[QStringLiteral("temp")] = QStringLiteral("--"); // needs WMI/driver access; left out
    map[QStringLiteral("uptime")] = QStringLiteral("--");

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (::GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
        const double used = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
        map[QStringLiteral("ram_pct")] = static_cast<int>(mem.dwMemoryLoad);
        map[QStringLiteral("ram_text")] = QStringLiteral("%1 / %2 GB")
            .arg(used / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1)
            .arg(static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    }

    // System drive (usually C:)
    const QString sysDrive = QString::fromLocal8Bit(qgetenv("SystemDrive")) + QStringLiteral("\\");
    ULARGE_INTEGER freeToCaller{}, total{}, freeTotal{};
    if (::GetDiskFreeSpaceExW(sysDrive.toStdWString().c_str(), &freeToCaller, &total, &freeTotal) && total.QuadPart > 0) {
        const unsigned long long used = total.QuadPart - freeTotal.QuadPart;
        map[QStringLiteral("disk_pct")] = static_cast<int>((used * 100) / total.QuadPart);
        map[QStringLiteral("disk_text")] = QStringLiteral("%1 / %2 GB")
            .arg(used / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0)
            .arg(total.QuadPart / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0);
    }

    const unsigned long long secs = ::GetTickCount64() / 1000;
    map[QStringLiteral("uptime")] = QStringLiteral("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
    return map;
}

QVariantMap ShellBridge::getUpdateInfo() const {
    // Windows Update has no cheap local "pending count"; show the up-to-date state
    QVariantMap map;
    map[QStringLiteral("has_updates")] = false;
    map[QStringLiteral("count")] = 0;
    map[QStringLiteral("icon_path")] = QString();
    map[QStringLiteral("tooltip")] = QStringLiteral("Windows Update — Click to Open");
    return map;
}

void ShellBridge::launchUpdate() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:windowsupdate")));
}

void ShellBridge::openSystemMonitor(const QString &section) {
    if (section == QLatin1String("gpu") || section == QLatin1String("cpu")) {
        // Task Manager opens on the Performance tab with /7 on Windows 11
        QProcess::startDetached(QStringLiteral("taskmgr.exe"), {QStringLiteral("/7")});
        return;
    }
    QProcess::startDetached(QStringLiteral("taskmgr.exe"), {});
}

void ShellBridge::updateTelemetry() {
    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (::GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        const unsigned long long idle = fileTimeToU64(idleFt);
        const unsigned long long total = fileTimeToU64(kernelFt) + fileTimeToU64(userFt); // kernel includes idle
        if (m_lastWinTotal != 0 && total > m_lastWinTotal) {
            const unsigned long long dTotal = total - m_lastWinTotal;
            const unsigned long long dIdle = idle >= m_lastWinIdle ? idle - m_lastWinIdle : 0;
            m_lastCpuPercent = static_cast<int>(((dTotal - std::min(dIdle, dTotal)) * 100) / dTotal);
        }
        m_lastWinIdle = idle;
        m_lastWinTotal = total;
    }
    // GPU load needs vendor APIs or PDH GPU counters; stays at 0 for now
    m_lastGpuPercent = 0;

    const int vol = m_audioEngine ? m_audioEngine->volume() : 10;
    const bool muted = m_audioEngine ? m_audioEngine->isMuted() : false;
    emit statsUpdated(m_lastCpuPercent, m_lastGpuPercent, vol, muted);

    QVariantMap metrics = getSysMonitorInfo();
    metrics[QStringLiteral("cpu")] = m_lastCpuPercent;
    metrics[QStringLiteral("gpu")] = m_lastGpuPercent;
    emit telemetryUpdated(metrics);
}

} // namespace ravenspot
