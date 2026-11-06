# Phase 3 Design: Right-click everything

**Date:** 2026-11-06 · **Size:** project · Planning delegated by the user; all calls below are mine.

## Intent
Right-clicking (or Menu / Shift+F10 on) any file, folder or app in RavenSpot should
offer what a file manager / app launcher offers, so the user rarely needs Dolphin.

## Current state (evidence)
- Apps: Launch, .desktop actions, Open Location, Run in Terminal, Software Center, Edit, Copy Path.
- Files/folders: Open, Open Containing Folder, Open in Terminal, Copy Path, Move to Trash.
- `openInTerminal(path)` builds `cd "<path>" && exec $SHELL` and runs it via `/bin/sh -c`
  (same expansion bug class fixed in phase 2); a path search row runs
  `konsole --workdir <path>` through `runDetached`.
- The popup is mouse-only: no keyboard navigation although Menu/Shift+F10 open it.

## Goals
- Files/folders: **Open With ▸** (apps whose MimeType matches the file's MIME or an
  ancestor; system default first), **Copy** (file itself to clipboard → paste in
  Dolphin), **Rename…** (inline field), **Compress to ZIP**, **Extract Here** (archives),
  **Properties** (type, size/items, modified, permissions, owner, symlink target).
- Apps: **Launch at Login** toggle (`~/.config/autostart`), **Uninstall…** (opens a
  terminal with `sudo pacman -Rns <owner pkg>` or `flatpak uninstall <id>`; the package
  manager asks for confirmation itself; hidden when no owner is found).
- Fix the shell-string paths for "Open in Terminal" and folder search rows (argv only).
- Keyboard: ↑/↓ move, Enter activates, Esc / ← go back from a submenu, else close.

## Non-goals / anti-goals
- No custom file operations engine (copy/move between folders), no permissions editor.
- Never delete without the existing trash path; never uninstall without the package
  manager's own prompt.
- No new link-time dependencies (no KF6Service): parse `.desktop` MimeType ourselves.

## Approaches
1. **Pure helpers + model builders (chosen):** `src/files/FileActions` (testable
   functions: validation, properties, archive detection, argv builders, open-with
   matching, uninstall command, autostart), QML entry lists from
   `MenuModel.fileContextEntries/appContextEntries`, popup gains modes
   (list / openWith / rename / properties) inside the same Popup.
2. KF6Service/KIO (KApplicationTrader, KPropertiesDialog): exact KDE behaviour but new
   heavy deps near the 50 MB limit and a widgets dialog from a layer-shell surface.
3. Shell out to `kioclient`/`dolphin` for everything: least code, but no Open With list,
   no inline rename/properties, Dolphin windows pop up.

## Decisions
- Open With runs `gio launch <desktop-file> <path>` (handles Exec field codes) via
  `systemd-run --user --scope` like other launches; default app from
  `xdg-mime query default <mime>`.
- Copy puts `text/uri-list` + `x-special/gnome-copied-files` + plain path on the clipboard.
- Rename rejects empty names, `/`, `.`/`..`, and existing targets; reports the error inline.
- Compress: `ark --batch --add --autofilename zip <path>`; Extract: `ark --batch --autodestination <path>`.
- Properties are shown as rows inside the popup with "Copy path" and "‹ Back".
- Desktop-file scan is lazy and cached; invalidated when apps are reindexed.
- UI strings English (Turkish suffixes kept on the existing labels only).
