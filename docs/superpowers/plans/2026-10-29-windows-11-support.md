# Windows 11 Cross-Platform Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform RavenSpot into a flawless cross-platform application that runs identically on Linux (KDE) and Windows 11, with native Windows hotkey, Start Menu apps & icon extraction, Explorer integration, and automated standalone `.exe` generation via GitHub Actions CI/CD.

**Architecture:** A unified `bridge.py` supporting both Linux and Windows with OS-specific abstraction methods for IPC, process launching, app discovery, icon resolution, audio control, and telemetry. Packaging is automated via PyInstaller and GitHub Actions CI.

**Tech Stack:** Python 3.10+, PySide6 (QtQuick/QML), PyInstaller, GitHub Actions, Win32 API (`ctypes`), `psutil`.

**Spec:** `docs/superpowers/specs/2026-10-29-windows-11-support-design.md`

## Global Constraints
- Unified codebase: Linux functionality must not degrade or break.
- Standalone `.exe` on Windows must bundle all dependencies, sounds, themes, and QML files.
- QML UI code (`Menu.qml`, `MenuModel.js`) must remain platform-agnostic.
- Windows file/folder actions must use native Windows mechanisms (`explorer.exe /select,`, Recycle Bin with undo).

## Review Focus
1. `_try_fast_client` IPC socket connection on Windows without AF_UNIX.
2. Windows Start Menu `.lnk` shortcut parsing with spaces or Unicode characters.
3. Windows Explorer path opening with spaces and `/select,` argument.
4. Win32 `RegisterHotKey` loop handling without freezing the Qt main loop.
5. PyInstaller bundle path resolution (`sys._MEIPASS`) for QML, sounds, and themes.

---

### Task 1: Cross-Platform IPC & Path Resolution
**Files:**
- Modify: `bridge.py`
- Test: `tests/test_platform_ipc.py`

**Interfaces:**
- Consumes: `_try_fast_client()`, `SOCKET_PATH`, `APP_DIR`, `USER_CONFIG_DIR`
- Produces: Platform-agnostic `QLocalServer`/`QLocalSocket` IPC mechanism for single-instance daemon toggle across Linux and Windows.

- [ ] **Step 1: Write test for IPC and path constants on Windows and Linux**
- [ ] **Step 2: Implement QLocalSocket-based `_try_fast_client()` and `QLocalServer` in `bridge.py`**
- [ ] **Step 3: Support PyInstaller bundled resource path (`getattr(sys, '_MEIPASS', APP_DIR)`)**
- [ ] **Step 4: Verify test passes**
- [ ] **Step 5: Commit changes**

---

### Task 2: Windows System Telemetry & Audio Control
**Files:**
- Modify: `bridge.py`
- Test: `tests/test_windows_telemetry.py`

**Interfaces:**
- Consumes: `_update_telemetry()`, `setVolume()`, `toggleMute()`, `getInitialAudioInfo()`
- Produces: Platform-swapped implementations using `psutil`/`ctypes` on Windows and `/proc`/`wpctl` on Linux.

- [ ] **Step 1: Write tests for telemetry extraction abstraction**
- [ ] **Step 2: Implement Windows CPU/RAM/Uptime providers via `ctypes` / `psutil`**
- [ ] **Step 3: Implement Windows audio volume and mute controls**
- [ ] **Step 4: Verify tests pass**
- [ ] **Step 5: Commit changes**

---

### Task 3: Windows Start Menu App Scanning & Icon Extraction
**Files:**
- Modify: `bridge.py`
- Test: `tests/test_windows_apps.py`

**Interfaces:**
- Consumes: `_load_apps()`, `listAppsCategory()`, `AppIconProvider`
- Produces: Windows Start Menu `.lnk` parsing into `{id, label, icon, desktop/target}` and Win32 `SHGetFileInfoW` icon extraction.

- [ ] **Step 1: Write test for Windows application discovery and icon extraction**
- [ ] **Step 2: Implement Start Menu shortcut scanner for `ProgramData` and `AppData`**
- [ ] **Step 3: Implement Win32 icon extractor returning `QImage` for `AppIconProvider`**
- [ ] **Step 4: Support Candy-icons local bundle fallback for `ThemeIconProvider`**
- [ ] **Step 5: Verify test passes**
- [ ] **Step 6: Commit changes**

---

### Task 4: Windows File Manager, Terminal & Right-Click Actions
**Files:**
- Modify: `bridge.py`
- Test: `tests/test_windows_actions.py`

**Interfaces:**
- Consumes: `openPath()`, `openContainingFolder()`, `openInTerminal()`, `moveToTrash()`
- Produces: Windows Explorer (`explorer.exe /select,`), Windows Terminal (`wt.exe` / `powershell.exe`), and Win32 Recycle Bin (`SHFileOperationW`).

- [ ] **Step 1: Write tests for Windows file/folder actions**
- [ ] **Step 2: Implement `openPath` and `openContainingFolder` using `explorer.exe` on Windows**
- [ ] **Step 3: Implement `openInTerminal` using `wt.exe` or `powershell.exe`**
- [ ] **Step 4: Implement `moveToTrash` using Win32 `SHFileOperationW` (FO_DELETE with FOF_ALLOWUNDO)**
- [ ] **Step 5: Verify tests pass**
- [ ] **Step 6: Commit changes**

---

### Task 5: Windows Native Global Hotkey (`RegisterHotKey`)
**Files:**
- Modify: `bridge.py`
- Test: `tests/test_hotkey_adapter.py`

**Interfaces:**
- Consumes: `shell.toggleRequested`
- Produces: Native Win32 `RegisterHotKey` listener running in a dedicated `QThread` on Windows to trigger `Alt+Space` / `Win+Space`.

- [ ] **Step 1: Write test for hotkey thread lifecycle**
- [ ] **Step 2: Implement Win32 `RegisterHotKey` and `GetMessageW` loop in `bridge.py`**
- [ ] **Step 3: Connect hotkey trigger to `shell.toggleRequested.emit()`**
- [ ] **Step 4: Verify test passes**
- [ ] **Step 5: Commit changes**

---

### Task 6: Packaging, Local `build.bat`, and GitHub Actions CI/CD Workflow
**Files:**
- Create: `build.bat`
- Create: `.github/workflows/build-windows.yml`
- Create: `requirements.txt`
- Modify: `README.md`

**Interfaces:**
- Produces: Standalone `RavenSpot.exe` build pipeline and downloadable GitHub Release asset.

- [ ] **Step 1: Create `requirements.txt` (PySide6, psutil, pywin32 optional/fallback)**
- [ ] **Step 2: Create `build.bat` for local one-click Windows builds with PyInstaller**
- [ ] **Step 3: Create `.github/workflows/build-windows.yml` for automated GitHub Releases**
- [ ] **Step 4: Update `README.md` with Windows installation instructions**
- [ ] **Step 5: Commit and tag/prepare for push**
