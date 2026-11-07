#include "core/WinUtil.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace ravenspot::win {

namespace {
std::wstring wide(const QString &s) { return s.toStdWString(); }
HWND hwndOf(QWindow *w) { return w ? reinterpret_cast<HWND>(w->winId()) : nullptr; }

// PowerShell -EncodedCommand takes UTF-16LE base64: no quoting/escaping issues at all.
QString encodedPowerShell(const QString &command) {
    const QByteArray utf16(reinterpret_cast<const char *>(command.utf16()), command.size() * 2);
    return QString::fromLatin1(utf16.toBase64());
}
} // namespace

void forceForeground(QWindow *window) {
    HWND hwnd = hwndOf(window);
    if (!hwnd) return;
    if (::SetForegroundWindow(hwnd)) return;
    // Foreground lock: attach to the current foreground thread's input queue,
    // then the call is allowed (standard launcher technique).
    HWND fg = ::GetForegroundWindow();
    const DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD self = ::GetCurrentThreadId();
    if (fgThread && fgThread != self) ::AttachThreadInput(self, fgThread, TRUE);
    ::BringWindowToTop(hwnd);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);
    if (fgThread && fgThread != self) ::AttachThreadInput(self, fgThread, FALSE);
}

void applyWindowStyle(QWindow *window) {
    HWND hwnd = hwndOf(window);
    if (!hwnd) return;
    // Windows 11: rounded corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2)
    const DWORD corner = 2;
    ::DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    const QString mode = qEnvironmentVariable("RAVENSPOT_WIN_BACKDROP").toLower();
    if (mode != QLatin1String("acrylic") && mode != QLatin1String("mica")) return;
    // DWMWA_SYSTEMBACKDROP_TYPE = 38: 2 = Mica, 3 = Acrylic (transient window). Windows 11 22H2+.
    const MARGINS margins{-1, -1, -1, -1};
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    const DWORD backdrop = (mode == QLatin1String("mica")) ? 2 : 3;
    ::DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    const BOOL dark = TRUE; // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
}

bool shellExecute(const QString &file, const QString &params, const QString &verb, const QString &workDir) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | (verb == QLatin1String("properties") ? SEE_MASK_INVOKEIDLIST : 0);
    const std::wstring wVerb = wide(verb), wFile = wide(QDir::toNativeSeparators(file)),
                       wParams = wide(params), wDir = wide(QDir::toNativeSeparators(workDir));
    sei.lpVerb = wVerb.c_str();
    sei.lpFile = wFile.c_str();
    sei.lpParameters = params.isEmpty() ? nullptr : wParams.c_str();
    sei.lpDirectory = workDir.isEmpty() ? nullptr : wDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ::ShellExecuteExW(&sei) != FALSE;
}

bool openTerminal(const QString &command, const QString &workDir) {
    const QString dir = workDir.isEmpty() ? QDir::homePath() : workDir;
    QStringList psArgs = {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")};
    if (!command.trimmed().isEmpty()) {
        psArgs << QStringLiteral("-EncodedCommand") << encodedPowerShell(command);
    }
    const QString wt = QStandardPaths::findExecutable(QStringLiteral("wt"));
    if (!wt.isEmpty()) {
        QStringList args = {QStringLiteral("-d"), QDir::toNativeSeparators(dir), QStringLiteral("powershell.exe")};
        args << psArgs;
        if (QProcess::startDetached(wt, args, dir)) return true;
    }
    // Plain console: start through cmd's "start" so a new console window is created.
    QProcess proc;
    proc.setProgram(QStringLiteral("powershell.exe"));
    proc.setArguments(psArgs);
    proc.setWorkingDirectory(dir);
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NEW_CONSOLE;
    });
    return proc.startDetached();
}

// Resolves a .lnk to its target path and arguments (COM IShellLink).
bool resolveShortcut(const QString &lnkPath, QString *target, QString *args, QString *workDir) {
    const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW *link = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                     reinterpret_cast<void **>(&link)))) {
        IPersistFile *file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file)))) {
            const std::wstring w = QDir::toNativeSeparators(lnkPath).toStdWString();
            if (SUCCEEDED(file->Load(w.c_str(), STGM_READ))) {
                wchar_t buf[MAX_PATH * 2] = {0};
                // SLGP_RAWPATH keeps %ProgramFiles% style paths unexpanded: expand ourselves
                if (SUCCEEDED(link->GetPath(buf, MAX_PATH * 2, nullptr, 0))) {
                    if (target) *target = QDir::fromNativeSeparators(QString::fromWCharArray(buf));
                }
                wchar_t argBuf[2048] = {0};
                if (args && SUCCEEDED(link->GetArguments(argBuf, 2048))) *args = QString::fromWCharArray(argBuf);
                wchar_t dirBuf[MAX_PATH * 2] = {0};
                if (workDir && SUCCEEDED(link->GetWorkingDirectory(dirBuf, MAX_PATH * 2)))
                    *workDir = QDir::fromNativeSeparators(QString::fromWCharArray(dirBuf));
                ok = true;
            }
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init)) ::CoUninitialize();
    return ok;
}

QString gitBashPath() {
    QStringList candidates;
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!git.isEmpty()) {
        // <Git>/cmd/git.exe -> <Git>/bin/bash.exe
        candidates << QFileInfo(git).absolutePath() + QStringLiteral("/../bin/bash.exe");
    }
    candidates << QStringLiteral("C:/Program Files/Git/bin/bash.exe")
               << QString::fromLocal8Bit(qgetenv("LOCALAPPDATA")) + QStringLiteral("/Programs/Git/bin/bash.exe");
    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.isFile()) return fi.canonicalFilePath();
    }
    return QString();
}

void revealInExplorer(const QString &path) {
    const QFileInfo fi(path);
    if (!fi.exists()) {
        shellExecute(fi.absolutePath());
        return;
    }
    // explorer wants /select,"C:\a b\c" literally; QProcess would quote the whole argument.
    QProcess proc;
    proc.setProgram(QStringLiteral("explorer.exe"));
    proc.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fi.absoluteFilePath())));
    proc.startDetached();
}

} // namespace ravenspot::win
