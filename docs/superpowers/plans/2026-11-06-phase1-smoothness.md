# Phase 1 Smoothness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Typing updates result rows in place (no delegate churn), open/selection motion is soft, first open has no audio-init hitch, and the `Invalid path data` warning is gone.

**Architecture:** A pure JS helper `MenuModel.syncRows(model, rows)` replaces every `displayModel.clear()` + `append` in `rebuildDisplay()`, writing rows over existing indices with a fixed role set. Animation constants change in `Menu.qml`. `AudioEngine` gets an explicit warm-up invoked from the event loop after daemon start.

**Tech Stack:** Qt 6.12 Quick/QML, C++20, qmltestrunner, existing Python E2E suite.

**Spec:** `docs/superpowers/specs/2026-11-06-phase1-smoothness-design.md`

## Global Constraints

- Open: opacity 0→1 + scale 0.97→1.0, 180 ms `Easing.OutQuint`; close 120 ms `Easing.InCubic`; `reopenAnim` uses the same curves.
- Card height: 130 ms `Easing.OutQuint`.
- Selection highlight move: 110 ms `OutCubic` (was 55 ms).
- Tail-inserted rows fade in: opacity 0→1, 120 ms `OutCubic`; no move/displaced transitions.
- No change to result content or ordering. Both repos (`ravenspot`, `omarchyapp`) stay byte-identical for `src/`, `Menu.qml`, `MenuModel.js`; never copy `menu.jsonc` to the mirror.
- E2E suite: 228/228; builds warning-free.

## Review Focus

1. A row type with extra roles (file row: `filePath`, `isDir`) replaced by one without them must not keep the stale values (wrong context-menu target). → Task 1 test `test_staleRolesReset`.
2. Shrinking from N rows to 0 (query cleared / no results) must leave `count === 0`. → Task 1 test `test_shrinkToEmpty`.
3. Selection after an in-place update: `selectedIndex` beyond the new count must be clamped by `settleCursor()`. → Task 2 Step 4 manual check + existing E2E.
4. Async results (window/file search) still rebuild through the same path and keep the selected `itemId`. → Task 2 Step 4.
5. Mirror drift: `menu.jsonc` differs between repos and must not be overwritten. → Task 6 sync step copies an explicit file list.

---

### Task 1: `syncRows` helper

**Files:**
- Modify: `MenuModel.js` (append function)
- Create: `tests/qml/tst_syncrows.qml`

**Interfaces:**
- Produces: `syncRows(model: ListModel, rows: Array<Object>) -> void`. After the call `model.count === rows.length` and `model.get(i)[r] === rows[i][r]` for every role `r` in `ROW_ROLES`; a role missing from `rows[i]` is written as its default from `ROW_DEFAULTS`.
- Produces: `ROW_DEFAULTS` object: every role currently emitted by `displayRow()` and the special rows in `rebuildDisplay()` (strings `""`, numbers `0`, booleans `false`; includes `filePath: ""`, `isDir: false`, `section: ""`, `score: 0`, `disabled: false`, `childCount: 0`).

- [ ] **Step 1: Write the failing tests** in `tests/qml/tst_syncrows.qml` (`TestCase`, `import "../../MenuModel.js" as MenuModel`, a `ListModel { id: m }`):
  - `test_growAndUpdate`: start with 2 rows, sync 4 → `count 4`, `get(0).label` equals new label.
  - `test_shrinkToEmpty`: 3 rows → sync `[]` → `count 0`.
  - `test_staleRolesReset`: sync `[{itemId:"f", filePath:"/x", isDir:true}]` then `[{itemId:"a", label:"App"}]` → `get(0).filePath === ""`, `get(0).isDir === false`.
  - `test_inPlaceKeepsCount`: 3 rows → sync 3 different rows → `count 3` and no `rowsRemoved` signal fired (connect `m.rowsRemoved` to a counter; expect 0).
- [ ] **Step 2: Run, expect FAIL** — `/usr/lib/qt6/bin/qmltestrunner -input tests/qml/tst_syncrows.qml` → fails with `syncRows is not a function`.
- [ ] **Step 3: Implement** `syncRows` / `ROW_DEFAULTS` in `MenuModel.js`: normalise each row against `ROW_DEFAULTS`, `model.set(i, row)` for `i < min`, `model.append` for the tail, `model.remove(rows.length, model.count - rows.length)` when shrinking.
- [ ] **Step 4: Run, expect PASS** — same command → `Totals: 4 passed, 0 failed`.
- [ ] **Step 5: Commit** — `git add MenuModel.js tests/qml/tst_syncrows.qml && git commit -m "feat(model): add in-place syncRows with fixed role defaults"`.

### Task 2: Use `syncRows` in `rebuildDisplay()` + timing log

**Files:**
- Modify: `Menu.qml` — `rebuildDisplay()` (starts ~line 570) and every early-return branch that does `displayModel.append` loops.

**Interfaces:**
- Consumes: `MenuModel.syncRows(displayModel, rows)` from Task 1.
- Produces: env var `RAVENSPOT_PERF=1` → `console.log("rebuildDisplay", ms, "ms rows=", n)` via `Shell` env read; off by default.

- [ ] **Step 1: Add the timing log first** (wrap `rebuildDisplay` body with `Date.now()`; read the flag once via a new `Q_INVOKABLE bool ShellBridge::perfLogging() const` returning `qEnvironmentVariableIsSet("RAVENSPOT_PERF")`), build, deploy with `RAVENSPOT_PERF=1` in a drop-in, type `fire`, `term`, `set` and record the numbers (baseline).
- [ ] **Step 2: Replace** `displayModel.clear()` at the top and each `for (...) displayModel.append(rows[m])` with collecting into `rows` and a single `MenuModel.syncRows(displayModel, rows)` before `settleCursor()`.
- [ ] **Step 3: Add** the tail fade: delegate `opacity` starts 0 via `ListView.onAdd` → `NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }`.
- [ ] **Step 4: Verify** — rebuild, repeat the Step 1 sequence and record numbers (expect lower); manual: type fast, arrow down 5 rows then type one more letter (selection clamps, no crash), `*conf` file search and a window-title query (async rows appear, selection kept). Remove the perf drop-in.
- [ ] **Step 5: Commit** — `git commit -am "perf(list): update rows in place instead of rebuilding delegates per keystroke"`.

### Task 3: Soft animation curves

**Files:**
- Modify: `Menu.qml` — `reopenAnim` (~1796), card `Behavior on opacity/scale/height` (~1871-1873), `opened` scale binding `0.94` → `0.97`, `highlightMoveDuration` (~2594), close path.

- [ ] **Step 1: Apply** the Global Constraints values; close uses `InCubic 120`, open `OutQuint 180`; `highlightMoveDuration: 110` plus `highlight` `Behavior on y { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }` with `highlightFollowsCurrentItem` kept.
- [ ] **Step 2: Verify** — `qmllint Menu.qml` no new errors; E2E green; manual open/close ×5, arrows.
- [ ] **Step 3: Commit** — `git commit -am "style(motion): soft non-overshooting open, height and selection curves"`.

### Task 4: Audio warm-up

**Files:**
- Modify: `src/audio/AudioEngine.hpp/.cpp` — public `void warmUp();` (calls `setupVoices()`).
- Modify: `src/main.cpp` — after the engine is created in daemon mode: `QTimer::singleShot(250, audioEngine, &AudioEngine::warmUp);`.

- [ ] **Step 1: Implement** and build.
- [ ] **Step 2: Verify** — restart service; `journalctl` shows the FFmpeg banner within ~1 s of start, **before** the first `show` command; first open after restart plays sound with no visible hitch.
- [ ] **Step 3: Commit** — `git commit -am "perf(audio): warm up sound voices after daemon start"`.

### Task 5: `Invalid path data` warning

**Files:** whichever SVG is identified (expected: an icon rendered on open, e.g. the update badge `cachy-update-blue.svg` or a candy-icons file).

- [ ] **Step 1: Locate** — render each SVG referenced on the root screen with a small `QSvgRenderer` probe in the scratchpad and find the one printing the warning.
- [ ] **Step 2: Fix** — if the file is in this repo, correct its path data (or re-export via `rsvg-convert`/`svgo`); if it is a system file, stop referencing it and use the theme icon name instead.
- [ ] **Step 3: Verify** — restart, open menu, `journalctl _PID=<pid>` has no `Invalid path data`.
- [ ] **Step 4: Commit** — `git commit -am "fix(icons): remove malformed SVG path warning on open"`.

### Task 6: Mirror, full verification, deploy

- [ ] **Step 1: Sync** — copy exactly `Menu.qml MenuModel.js src/ tests/qml/` to `../omarchyapp/` (never `menu.jsonc`); `diff -rq src ../omarchyapp/src && diff -q Menu.qml ../omarchyapp/Menu.qml && diff -q MenuModel.js ../omarchyapp/MenuModel.js`.
- [ ] **Step 2: Build both** — `make -j$(nproc)` in each, no `error|warning`.
- [ ] **Step 3: Tests** — `qmltestrunner -input tests/qml/` all pass; `./tests/run_e2e_tests.sh` → 228/228.
- [ ] **Step 4: Deploy** — stop service, copy binary, start, `is-active` = `active`.
