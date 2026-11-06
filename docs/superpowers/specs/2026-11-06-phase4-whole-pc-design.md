# Phase 4 Design: "The whole PC from one place"

**Date:** 2026-11-06 · **Size:** project · Planning delegated by the user ("izinsiz"); all calls are mine.

## Intent
Finish the "do everything from RavenSpot" vision: the features whose UI already
exists but whose backends are stubs, plus the two most-requested launcher extras.

## Current state (evidence)
- `ShellBridge::listClipboard()` returns `"[]"` — the Clipboard menu is always empty.
- `ShellBridge::listProcesses()` returns `"[]"` — `kill <name>` never lists anything;
  `killProcess(pid)` (SIGTERM, pid>1) already exists.
- Web bangs (`yt `, `gh `, `map `, `ddg ` …) already exist.
- No recent-files or emoji search.
- The user runs `orkun-clipboard-daemon`, which broadcasts NDJSON clipboard events
  (`seq,timestamp,types,text,html,image_path,fingerprint,origin`) on
  `$XDG_RUNTIME_DIR/orkun-clipboard.sock` and already drops password-manager /
  concealed items. ClipBoardLine (Electron) keeps its own private store.
- Memory budget: daemon private dirty ≈ 47.8 MB of the 50 MB E2E limit.

## Goals
1. **Clipboard history:** subscribe to the orkun socket; keep the last 50 text
   entries in memory only (no disk), de-duplicated by fingerprint, each capped at
   4 KB; `clip <query>` / the Clipboard menu lists them; Enter copies back. Reconnect
   with back-off if the daemon restarts; silently empty if it is not running.
2. **Process kill:** `kill <name>` lists the user's own processes from `/proc`
   (name, PID, RSS), matching name/cmdline substring, sorted by RSS, max 15; Enter
   sends SIGTERM; a second Enter on the same row within 5 s sends SIGKILL.
3. **Recent files:** `recent <query>` (and rows under file search) from
   `~/.local/share/recently-used.xbel`, newest first, existing files only, max 20.
4. **Emoji:** `:<name>` searches a bundled compact emoji table (name + keywords →
   glyph); Enter copies the glyph. Loaded lazily on first `:` query.

## Non-goals / anti-goals
- No clipboard persistence to disk (privacy), no image history (text only).
- Never kill processes of other users or PID 1; never SIGKILL without a second Enter.
- No new runtime services; nothing loaded until first use (memory).

## Approaches (clipboard)
1. **Subscribe to orkun socket, in-memory ring (chosen):** reuses the user's daemon,
   privacy filter already applied, ~200 KB.
2. Read ClipBoardLine's private store: couples to an Electron app's internals.
3. Own `wl_data_control` listener: duplicates the daemon, more Wayland code.

## Verification
- C++ tests: NDJSON parsing/ring/dedup/cap, /proc parsing with a fake proc root,
  XBEL parsing with a fixture, emoji lookup ranking.
- QML tests for row builders; E2E 228/228; memory measured before/after.

## Decisions (my calls)
- Emoji data: a compact copy of GitHub's gemoji database (MIT, © GitHub) vendored as
  `assets/emoji.json` (`[[glyph, description, "aliases tags"], …]`), attribution in
  `assets/EMOJI-LICENSE`. Ranking: exact alias > alias prefix > tag/description word prefix.
- Prefixes: `clip ` (also the existing Clipboard menu), `kill `, `recent `, `:` (emoji).
- Clipboard socket: `$XDG_RUNTIME_DIR/orkun-clipboard.sock`, QLocalSocket, reconnect
  back-off 1 s → 30 s; lines > 1 MB dropped; only `text` (no html/images).
- Kill: SIGTERM on Enter; same row Enter again within 5 s → SIGKILL ("Force kill").
