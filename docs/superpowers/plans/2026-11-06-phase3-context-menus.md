# Phase 3 Context Menus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File/folder/app context menus with Open With, Copy, Rename, Compress/Extract, Properties, Launch at Login, Uninstall, keyboard navigation, and argv-safe terminal opening.
**Architecture:** `ravenspot::files` pure helpers (C++, unit-tested in `tests/cpp/cmdtests`), thin `ShellBridge` wrappers, pure QML entry builders in `MenuModel.js` (qmltest), popup modes in `Menu.qml`.
**Tech Stack:** C++20, Qt 6.12 (Core, Gui for QMimeData/QClipboard), QML, qmake.
**Spec:** `docs/superpowers/specs/2026-11-06-phase3-context-menus-design.md`

## Global Constraints
- No string-concatenated shell for any path/command; argv only.
- No new link-time dependencies. Re-run `qmake6` after adding any `#include` or source.
- Mirror: copy `src/ Menu.qml MenuModel.js *.qml tests/ ravenspot.pro` to `../omarchyapp/`; never `menu.jsonc`.
- UI strings English. E2E 228/228; private memory < 50 MB.

## Review Focus
1. Rename to an existing name, to `a/b`, to `..`, to empty → refused with message, nothing moved. → Task 1 `rename_rejects*`.
2. Open With for a file whose MIME only matches via an ancestor (e.g. `text/x-c++src` → `text/plain` editors). → Task 1 `openWith_matchesAncestorMime`.
3. Uninstall for a user-local `.desktop` (no package owner) → no Uninstall entry. → Task 1 `uninstall_noOwnerIsEmpty`, Task 3 `appEntries_hideUninstallWithoutOwner`.
4. Paths with spaces, quotes and `$(...)` in Open in Terminal / folder rows reach the terminal unexpanded. → Task 1 `terminalInDir_argvKeepsPath`.
5. Keyboard: Esc in a submenu returns to the main list instead of closing. → Task 4 manual + `contextBack` path.

### Task 1: `FileActions` helpers
Files: create `src/files/FileActions.hpp/.cpp`, `tests/cpp/tst_fileactions.cpp`; modify `tests/cpp/cmdtests.pro`, `tests/cpp/main.cpp`, `ravenspot.pro`.
Interfaces (`namespace ravenspot::files`):
- `QString validateNewName(const QString &dir, const QString &newName)` ("" = ok, else English message)
- `QString renamePath(const QString &path, const QString &newName)` ("" = ok)
- `QVariantMap properties(const QString &path)` keys: name, path, isDir, isSymlink, target, mime, mimeComment, size(qint64, -1 dir), sizeText, items(int, dirs), modified, permissions("rwxr-xr-x"), owner
- `bool isArchive(const QString &path)`
- `QStringList extractArgs(const QString &path)`, `QStringList compressArgs(const QString &path)` (program `ark`)
- `QStringList terminalInDirArgs(const QString &dir)` → `{"-e","bash","-c","cd -- \"$1\" && exec \"${SHELL:-bash}\" -i","ravenspot",dir}`
- `struct DesktopApp { QString file, name, icon; QStringList mimeTypes; bool noDisplay; };`
- `DesktopApp parseDesktopFile(const QString &file)`
- `QVariantList openWithApps(const QString &path, const QList<DesktopApp> &apps, const QString &defaultDesktopId)` → list of `{file,name,icon,isDefault}`, default first, then by name, NoDisplay excluded
- `QString autostartPathFor(const QString &desktopFile)`; `bool setAutostart(const QString &desktopFile, bool enabled)`; `bool isAutostart(const QString &desktopFile)` (autostart dir injectable via `QString autostartDir()` overridable by env `RAVENSPOT_AUTOSTART_DIR` for tests)
- `QString uninstallCommand(const QString &desktopFile, const std::function<QString(const QString &)> &pacmanOwner)` → `flatpak uninstall <id>` for `/flatpak/exports/share/applications/`, `sudo pacman -Rns <pkg>` when owner non-empty, else ""
Tests: `rename_ok`, `rename_rejectsExisting`, `rename_rejectsSlash`, `rename_rejectsDots`, `rename_rejectsEmpty`, `properties_fileAndDir`, `isArchive_detects`, `terminalInDir_argvKeepsPath`, `parseDesktop_readsMimeAndName`, `openWith_matchesExactAndDefaultFirst`, `openWith_matchesAncestorMime`, `autostart_roundTrip`, `uninstall_flatpakAndPacman`, `uninstall_noOwnerIsEmpty`. RED → implement → GREEN → commit.

### Task 2: ShellBridge wrappers + terminal fix
Q_INVOKABLE: `QVariantMap fileProperties(path)`, `QString renamePath(path,newName)`, `bool isArchive(path)`, `void extractHere(path)`, `void compressPath(path)`, `QString openWithApps(path)` (JSON), `void openWith(path, desktopFile)`, `void copyFileToClipboard(path)`, `bool isAutostart(desktop)`, `bool setAutostart(desktop,on)`, `bool canUninstall(desktop)`, `void uninstallApp(desktop)`; `openInTerminal(path)` uses `terminalInDirArgs`; `Q_INVOKABLE void openFolderInTerminal(path)` for the search row. Verify: build warning-free, cmdtests green, deploy smoke. Commit.

### Task 3: QML entry builders
`MenuModel.fileContextEntries(info)` (info: `{path,isDir,isArchive,isRootDownloads,inMenu}`) and `MenuModel.appContextEntries(info)` (`{desktop, actions:[{name,exec}], autostart:bool, canUninstall:bool}`) → arrays of `{label, icon, actionType, actionPayload, isDanger}`. Tests `tests/qml/tst_contextentries.qml`: `fileEntries_orderAndOpenWith`, `fileEntries_archiveShowsExtract`, `dirEntries_noOpenWithNoExtract`, `appEntries_autostartLabelToggles`, `appEntries_hideUninstallWithoutOwner`, `trashIsDanger`. RED → implement → GREEN; `openContextMenu` uses the builders. Commit.

### Task 4: Popup modes + keyboard
`root.contextMode`: "list" | "openWith" | "rename" | "properties"; actions `openWithMenu`, `openWithApp`, `contextBack`, `startRename`, `showProperties`, `copyFile`, `compress`, `extract`, `toggleAutostart`, `uninstall`. Rename row: TextInput prefilled with the name (selection before extension), Enter submits, error text shown, Esc back. Keyboard: Up/Down/Enter/Esc/Left on the popup. Verify qmllint, deploy, no QML errors on open. Commit.

### Task 5: Mirror, verification, deploy
Sync, `qmake6` both, clean build both, cmdtests + qmltestrunner + E2E, deploy, memory check.
