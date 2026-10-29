# RavenSpot Windows 11 Cross-Platform Support — Design Specification

## Goal
Enable RavenSpot to run seamlessly and natively on Windows 11 with full feature parity with the Linux/KDE version: same UI, glassmorphism cyberpunk themes, sound effects, system telemetry, downloads & app launcher, right-click context menus, Candy icon set, native Windows global hotkey (`Alt+Space` / `Win+Space`), and automated standalone `.exe` installer generation via GitHub Actions CI/CD.

## Architecture

### 1. Unified Cross-Platform Codebase
A single code repository serving both Linux (KDE Plasma) and Windows 11:
- `bridge.py` dynamically detects `sys.platform == "win32"` and swaps Linux-specific subprocess/DBus calls with Windows API / PowerShell / Win32 equivalents.
- `QLocalServer` & `QLocalSocket` are used uniformly across both platforms for fast IPC toggling (Named Pipes on Windows, UNIX domain sockets on Linux).
- QtQuick / QML (`Menu.qml`, `MenuModel.js`, `themes.json`) remains 100% portable with zero platform branches in core QML logic.

### 2. Windows 11 Subsystem Adapters

| Subsystem | Linux (Current) | Windows 11 (Adapter) |
|---|---|---|
| **Global Hotkey** | KDE `kglobalaccel` | Win32 `RegisterHotKey` loop or `keyboard` hook thread |
| **App Scanning** | `.desktop` files in `/usr/share/applications` | `.lnk` files in Start Menu (`ProgramData` & `AppData`) + UWP Apps |
| **App Icons** | QIcon freedesktop theme resolver | Win32 `ExtractIconExW` / `SHGetFileInfoW` via `AppIconProvider` |
| **Candy Icons** | `~/.local/share/icons/candy-icons` | Bundled repository directory `icons/candy-icons` fallback |
| **File Manager** | `dolphin --select <path>` | `explorer.exe /select,"<path>"` |
| **Terminal** | `konsole --workdir <dir>` | `wt.exe -d "<path>"` (Windows Terminal) / `powershell.exe` |
| **Move to Trash** | `gio trash` / `kioclient` | Win32 `SHFileOperationW` (Recycle Bin with undo) |
| **Telemetry (CPU/RAM)** | `/proc/stat`, `/proc/meminfo` | `psutil` / Win32 `GetSystemTimes` & `GlobalMemoryStatusEx` |
| **Audio (Volume/Mute)** | `wpctl` (PipeWire) | PowerShell `AudioDeviceCmdlets` / Win32 endpoint volume |

### 3. Build & Distribution
- **Local:** `build.bat` using PyInstaller to produce a standalone `dist/RavenSpot.exe`.
- **CI/CD:** `.github/workflows/build-windows.yml` running on GitHub `windows-latest` runners, uploading `RavenSpot-Windows-x64.zip` directly to GitHub Releases upon tag or push.
