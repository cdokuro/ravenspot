# Phase 4 Whole-PC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Working clipboard history, `kill`, recent files and emoji search.
**Architecture:** Four small C++ units under `src/pc/` (pure parsing + thin IO), unit-tested in `tests/cpp/cmdtests`; `ShellBridge` wrappers; row builders in `MenuModel.js` (qmltest); `Menu.qml` wiring.
**Tech Stack:** C++20, Qt 6.12 (Core, Network for QLocalSocket), QML.
**Spec:** `docs/superpowers/specs/2026-11-06-phase4-whole-pc-design.md`

## Global Constraints
- Nothing loaded/connected before first use except the clipboard subscription (needed to collect history); private memory < 50 MB (now 47.8 MB) — measure before/after.
- argv only; no disk persistence of clipboard; never signal other users' processes or PID ≤ 1.
- Re-run `qmake6` after adding sources/includes. Mirror per earlier phases; never copy `menu.jsonc`.

## Review Focus
1. Clipboard: duplicate copies (same fingerprint) move to front instead of duplicating; a 10 MB text is capped at 4 KB; daemon restart → reconnect. → T1 tests.
2. Kill: query matching `ravenspot` itself or the user's shell is listed but other users' processes never are; PID 1 never. → T2 `list_ownUidOnly`, `list_excludesPid1`.
3. Recent: entries for deleted files are skipped; percent-encoded non-ASCII paths (`%C4%B0ndirilenler`) decode. → T3 tests.
4. Emoji: `:+1` / `:heart` rank exact aliases first; empty `:` lists nothing heavy (top 20 only). → T4 tests.
5. Second Enter on a kill row within 5 s escalates; on a different row it does not. → T5 qml test.

### T1 `src/pc/ClipboardHistory.{hpp,cpp}`
`struct ClipEntry { QString text; QString fingerprint; qint64 timestamp; }`; `class ClipboardHistory : QObject` — `static std::optional<ClipEntry> parseEvent(const QByteArray &line)`, `void ingest(const ClipEntry &)`, `QList<ClipEntry> entries() const`, `QList<ClipEntry> matching(const QString &q, int limit) const`, `void connectTo(const QString &socketPath)`, `static QString defaultSocketPath()`, signal `changed()`; constants `kMax = 50`, `kMaxChars = 4096`. Tests: `parse_textEvent`, `parse_ignoresNonText`, `parse_rejectsGarbage`, `ingest_dedupByFingerprint`, `ingest_capsLengthAndCount`, `socket_receivesEvents` (QLocalServer fake writes two NDJSON lines; spy `changed`), `socket_reconnects` (server restarted → new event arrives).

### T2 `src/pc/ProcessList.{hpp,cpp}`
`QVariantList listProcesses(const QString &query, const QString &procRoot, uint uid, int limit = 15)` → `{pid:int, name, cmd, rssKb:int, rssText}` sorted by rssKb desc. Tests with a fake proc tree in QTemporaryDir: `list_matchesNameAndCmdline`, `list_ownUidOnly`, `list_excludesPid1`, `list_sortedByRss`, `list_emptyQueryIsEmpty`.

### T3 `src/pc/RecentFiles.{hpp,cpp}`
`QVariantList recentFiles(const QString &xbelPath, const QString &query, int limit = 20)` → `{path, name, isDir, modified}` newest first, existing only, query substring on name. Tests (fixture XBEL): `recent_newestFirst`, `recent_skipsMissing`, `recent_decodesPercent`, `recent_filtersQuery`, `recent_missingFileIsEmpty`.

### T4 `src/pc/EmojiIndex.{hpp,cpp}` + `assets/emoji.json` + `assets/EMOJI-LICENSE`
`class EmojiIndex` — `bool load(const QString &jsonPath)`, `QVariantList search(const QString &q, int limit = 20) const` → `{glyph, name}`; lazily loaded by ShellBridge. Tests (small fixture JSON): `search_exactAliasFirst`, `search_prefix`, `search_tagMatch`, `search_emptyIsTop`, `load_missingFileFalse`.

### T5 ShellBridge + QML
Q_INVOKABLE `listClipboard()` (JSON from history, replaces stub), `clipboardMatching(q)`, `listProcesses(q)` (replaces stub; procRoot "/proc", getuid()), `forceKillProcess(pid)` (SIGKILL, same guards), `recentFiles(q)`, `searchEmoji(q)`. `MenuModel.pcRows(query, data)` builds rows for `clip `, `kill `, `recent `, `:`; `MenuModel.killDecision(lastKill, rowId, nowMs)` → "term" | "kill". QML tests `tst_pcrows.qml`: `clipRows`, `killRows`, `recentRows`, `emojiRows`, `killSecondEnterEscalates`, `killOtherRowDoesNot`.

### T6 Mirror, verification, deploy
Sync (incl. `assets/emoji.json`, `assets/EMOJI-LICENSE`), qmake6 both, clean build, cmdtests + qmltest + E2E, memory before/after, deploy.
