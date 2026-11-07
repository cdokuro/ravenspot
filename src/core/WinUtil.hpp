#pragma once
// Small Win32 helpers shared by the Windows port (window focus, DWM backdrop,
// shell launching). Only compiled on Windows (see ravenspot.pro win32 scope).

#include <QString>
#include <QStringList>
#include <QWindow>

namespace ravenspot::win {

// Brings the window to the foreground even when RavenSpot is a background
// process (Windows' foreground lock otherwise only flashes the taskbar).
void forceForeground(QWindow *window);

// Optional DWM backdrop behind the card. mode: "none" (default), "acrylic",
// "mica". Read from RAVENSPOT_WIN_BACKDROP; rounded corners are always asked for.
void applyWindowStyle(QWindow *window);

// ShellExecuteW wrapper: verb "open"/"runas"/"properties"/... Returns true on success.
bool shellExecute(const QString &file, const QString &params = QString(),
                  const QString &verb = QStringLiteral("open"), const QString &workDir = QString());

// Opens a new console window running PowerShell with the command; the window
// stays open afterwards (-NoExit). Uses Windows Terminal when installed.
bool openTerminal(const QString &command, const QString &workDir);

// .lnk -> target path (+ optional arguments / working directory). COM per call.
bool resolveShortcut(const QString &lnkPath, QString *target, QString *args = nullptr, QString *workDir = nullptr);

// Git for Windows' bash.exe if installed (not WSL's System32\bash.exe), else "".
QString gitBashPath();

// Explorer with the item selected.
void revealInExplorer(const QString &path);

} // namespace ravenspot::win
