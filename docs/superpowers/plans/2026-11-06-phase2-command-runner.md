# Phase 2 Command Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Paste or type any shell command into RavenSpot and run it correctly — in a terminal (Enter) or inline with streamed output (Shift+Enter) — with detection, history and a danger guard.

**Architecture:** Pure C++ analysis functions (`CommandAnalyzer`) decide detection, danger, terminal-routing and argv; a `CommandRunner` QObject owns the single inline QProcess; `CommandHistory` persists recent commands. `ShellBridge` exposes them to QML; `Menu.qml` builds the run/history rows, handles Enter/Shift+Enter/arming, and shows an output panel.

**Tech Stack:** C++20, Qt 6.12 (Core, QtTest), QML, qmake.

**Spec:** `docs/superpowers/specs/2026-11-06-phase2-command-runner-design.md`

## Global Constraints

- Command text reaches bash as ONE argv element; RavenSpot never runs commands through a string-concatenated `/bin/sh -c`.
- Terminal script: `<cmd>` + `\nprintf "\n[exit %s] " $?; exec "${SHELL:-bash}" -i`, launched as `term -e bash -c <script>`.
- Inline: `bash -c <cmd>`, working dir `$HOME`, merged channels, UTF-8, cap 2000 lines (oldest dropped), Stop = SIGTERM then SIGKILL after 2000 ms, one inline run at a time, notification on finish when the menu is closed.
- History: `~/.local/share/ravenspot/command_history.json`, newest first, de-duplicated, max 50, written when a run starts.
- Bare single word never yields a run row without `>`.
- UI strings in English.
- Private memory stays < 50 MB (E2E f20); idle cost of the feature ≈ 0 (no QProcess until used).
- Mirror: copy `src/ Menu.qml MenuModel.js OpenCloseAnimation.qml tests/` explicitly to `../omarchyapp/`; never `menu.jsonc`.

## Review Focus

1. A command containing `'`, `"`, `$`, backtick, `\` and newlines arrives at bash byte-identical. → Task 1 `argv_preservesSpecialChars`.
2. `firefox` (app) typed alone shows no run row; `firefox --private-window` shows a weak (non-first) row. → Task 1 `detect_bareWordIsNone`, `detect_execWithArgsIsWeak`.
3. Danger matching has no false positives on normal paths (`rm -rf ~/tmp/x`, `rm -rf ./build`, `dd if=a of=b.img`). → Task 1 `danger_noFalsePositives`.
4. Stop on a process that ignores SIGTERM still ends it (SIGKILL after 2 s). → Task 2 `stop_escalatesToKill`.
5. Starting a second inline run while one is running is refused (returns false) instead of orphaning the first. → Task 2 `start_refusedWhileRunning`.

---

### Task 1: `CommandAnalyzer` (pure functions) + C++ test harness

**Files:**
- Create: `src/run/CommandAnalyzer.hpp`, `src/run/CommandAnalyzer.cpp`
- Create: `tests/cpp/cmdtests.pro`, `tests/cpp/main.cpp`, `tests/cpp/tst_commandanalyzer.cpp`
- Modify: `ravenspot.pro` (add sources)

**Interfaces (namespace `ravenspot::cmd`):**
- `enum class Signal { None = 0, Weak = 1, Strong = 2 };`
- `Signal detect(const QString &text, const std::function<bool(const QString &)> &isExecutable);`
- `bool isDangerous(const QString &command);`
- `bool needsTerminal(const QString &command);` — true for leading `sudo`/`doas`/`pkexec` or first token in {htop, btop, top, vim, nvim, vi, nano, less, more, man, ssh, journalctl -f, watch, tmux, screen, mc, ranger, nmtui}.
- `QStringList terminalArgs(const QString &command);` — `{"-e", "bash", "-c", script}` per Global Constraints.
- `QStringList inlineArgs(const QString &command);` — `{"-c", command}` (program `bash`).
- `QString firstLine(const QString &command);` / `int extraLineCount(const QString &command);` (non-empty lines after the first).
- `QVariantMap analyze(const QString &text, const std::function<bool(const QString &)> &isExecutable);` → `{signal:int, dangerous:bool, needsTerminal:bool, firstLine:QString, extraLines:int}`.

- [ ] **Step 1: Write failing tests** in `tst_commandanalyzer.cpp` (class `TestCommandAnalyzer`, `isExecutable` stub = name ∈ {ls, git, firefox, pacman, echo, rm, dd}):
  - `detect_bareWordIsNone`: `"firefox"` → None; `"   "` → None.
  - `detect_execWithArgsIsWeak`: `"firefox --private-window"` → Weak; `"git status"` → Weak; `"unknowncmd foo"` → None.
  - `detect_strongSignals`: `"sudo pacman -Syu"`, `"ls | head"`, `"a && b"`, `"echo a\necho b"`, `"./run.sh"`, `"~/bin/x"`, `"echo $(date)"` → Strong.
  - `danger_matches`: `"rm -rf /"`, `"sudo rm -rf /*"`, `"rm -rf ~"`, `"rm -rf $HOME"`, `"mkfs.ext4 /dev/sda1"`, `"dd if=/dev/zero of=/dev/nvme0n1"`, `"echo x > /dev/sda"`, `":(){ :|:& };:"`, `"chmod -R 777 /"`, `"sudo chown -R me /"` → true.
  - `danger_noFalsePositives`: `"rm -rf ~/tmp/x"`, `"rm -rf ./build"`, `"dd if=a of=b.img"`, `"chmod -R 755 ~/proj"`, `"ls /dev/sda"` → false.
  - `needsTerminal_cases`: `"sudo ls"`, `"htop"`, `"ssh host"`, `"journalctl -f"` → true; `"ls -la"`, `"journalctl -b"` → false.
  - `argv_preservesSpecialChars`: for `cmd = "echo '$x' \"$(date)\" `+"`id`"+` \\n\nprintf ok"`, `terminalArgs(cmd)` == `{"-e","bash","-c", cmd + "\nprintf \"\\n[exit %s] \" $?; exec \"${SHELL:-bash}\" -i"}` and `inlineArgs(cmd)` == `{"-c", cmd}` (argv equality is the contract).
  - `lines_firstAndExtra`: `"a\n\nb\nc"` → firstLine `"a"`, extraLineCount `2`.
  - `analyze_map`: `analyze("sudo rm -rf /")` → `signal==2`, `dangerous==true`, `needsTerminal==true`; `analyze("ls -la")` → `signal==1`, `dangerous==false`, `extraLines==0`.
- [ ] **Step 2: Build & run, expect FAIL** — `cd tests/cpp && qmake6 && make -s && ./cmdtests` → link errors / missing symbols (functions not defined).
- [ ] **Step 3: Implement** in `CommandAnalyzer.cpp`. Detection: trim; empty → None; strong if regex `(^|\s)(sudo|doas|pkexec)\s` at start, or contains any of `|`, `&&`, `||`, `;`, `$(`, backtick, `\n`, ` > `, ` < `, or starts with `./`, `~/`, `/`; else split on whitespace: ≥2 tokens and `isExecutable(token0)` → Weak; else None. Danger: case-sensitive regexes over the whole text (strip leading `sudo `), each anchored so `~/x`/`./x` paths do not match (e.g. `rm\s+-[a-zA-Z]*r[a-zA-Z]*f?\s+(/|/\*|~|\$HOME)(\s|$)`).
- [ ] **Step 4: Run, expect PASS** — `./cmdtests` → `Totals: N passed, 0 failed`.
- [ ] **Step 5: Commit** — `git add src/run tests/cpp ravenspot.pro && git commit -m "feat(run): command analysis (detect, danger, terminal routing, argv)"`.

### Task 2: `CommandRunner` (inline QProcess)

**Files:**
- Create: `src/run/CommandRunner.hpp`, `src/run/CommandRunner.cpp`, `tests/cpp/tst_commandrunner.cpp`
- Modify: `ravenspot.pro`, `tests/cpp/cmdtests.pro`, `tests/cpp/main.cpp`

**Interfaces:**
- `class CommandRunner : public QObject` with `Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)`, `Q_PROPERTY(QString output READ output NOTIFY outputChanged)`, `Q_PROPERTY(QString command READ command NOTIFY runningChanged)`, `Q_PROPERTY(int exitCode READ exitCode NOTIFY finished)`.
- `Q_INVOKABLE bool start(const QString &command);` — false if already running or empty.
- `Q_INVOKABLE void stop();` · `Q_INVOKABLE void clear();`
- signals `outputChanged()`, `runningChanged()`, `finished(int exitCode, bool crashed)`.
- `static constexpr int kMaxLines = 2000;` `static constexpr int kKillGraceMs = 2000;` Output updates coalesced at most every 50 ms.

- [ ] **Step 1: Failing tests** (`TestCommandRunner`, use `QSignalSpy`):
  - `start_capturesOutputAndExit`: `start("echo hi; echo err >&2; exit 3")` → `finished` within 3000 ms, `exitCode() == 3`, `output()` contains `"hi"` and `"err"`.
  - `start_refusedWhileRunning`: `start("sleep 5")` true, second `start("echo x")` false; then `stop()`.
  - `stop_escalatesToKill`: `start("trap '' TERM; sleep 30")`, wait 200 ms, `stop()` → `finished` within 3500 ms.
  - `output_cappedAtMaxLines`: `start("seq 1 2500")` → after finish, line count == 2000 and first line == `"501"`.
  - `start_specialCharsUnexpanded`: `start("printf '%s' '$HOME'")` → output == `"$HOME"`.
- [ ] **Step 2: Run, expect FAIL** (undefined `CommandRunner`).
- [ ] **Step 3: Implement**: QProcess created per run, `setWorkingDirectory(QDir::homePath())`, `MergedChannels`, program `bash` with `cmd::inlineArgs`; `readyRead` appends to a line buffer trimmed to `kMaxLines`; 50 ms single-shot timer emits `outputChanged`; `stop()` → `terminate()` + `QTimer::singleShot(kKillGraceMs, kill)`.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(run): inline command runner with capped streamed output and stop"`.

### Task 3: `CommandHistory`

**Files:** Create `src/run/CommandHistory.hpp/.cpp`, `tests/cpp/tst_commandhistory.cpp`; modify `.pro` files, `tests/cpp/main.cpp`.

**Interfaces:**
- `explicit CommandHistory(QString filePath, QObject *parent = nullptr);` (default path from Global Constraints via `static QString defaultPath()`).
- `void add(const QString &command);` (trim; skip empty; move to front if present; cap 50; save atomically with `QSaveFile`)
- `QStringList entries() const;` · `QStringList matching(const QString &query, int limit = 5) const;` (case-insensitive substring, newest first)

- [ ] **Step 1: Failing tests** (temp dir via `QTemporaryDir`): `add_dedupMovesToFront`, `add_capsAt50`, `persist_roundTrip` (new instance reads same entries), `matching_caseInsensitiveNewestFirst`, `load_corruptFileIsEmpty` (file with `{garbage` → `entries()` empty, no crash).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(run): persistent command history"`.

### Task 4: ShellBridge wiring + terminal launch fix

**Files:** Modify `src/ui/ShellBridge.hpp/.cpp`, `src/main.cpp` (construct runner/history, pass to bridge).

**Interfaces (Q_INVOKABLE unless noted):**
- `QVariantMap analyzeCommand(const QString &text) const;` → `cmd::analyze(text, isExecutable)` with `isExecutable` = `!QStandardPaths::findExecutable(tok).isEmpty()` (cached per token in a `QHash`, cleared when the menu hides).
- `void runCommandInTerminal(const QString &command);` — resolves terminal like `runInTerminal`, `QProcess::startDetached(term, cmd::terminalArgs(command), QDir::homePath())`, then `history.add`.
- `bool runCommandInline(const QString &command);` — if `needsTerminal` → `runCommandInTerminal`, return false; else `runner.start`, `history.add`.
- `void stopInlineCommand();` · `QStringList commandHistory(const QString &query) const;` (empty query → first 10 entries).
- `Q_PROPERTY(QObject *runner READ runner CONSTANT)`.
- On `runner.finished` while menu hidden → `notify-send -a RavenSpot -i utilities-terminal "Command finished" "<firstLine> — exit <code>"` (argv, not shell).

- [ ] **Step 1: Implement** the wiring (logic is covered by Task 1-3 tests; ShellBridge links the whole app and is not unit-tested here).
- [ ] **Step 2: Verify** — app builds warning-free; `./cmdtests` still passes; deploy and `--show`/`--hide` log no errors.
- [ ] **Step 5: Commit** — `git commit -m "feat(run): expose command analysis, terminal/inline run and history to QML"`.

### Task 5: QML rows, keys, arming, history

**Files:** Modify `Menu.qml` (`rebuildDisplayInner` — replace the `// 7. Terminal komut modu` block; key handler at the Enter branches ~line 2016; `activateIndex`); Create `tests/qml/tst_runrows.qml` if row building is extracted to `MenuModel.js`.

**Interfaces:**
- `MenuModel.commandRows(query, analysis, history) -> Array<row>` (pure, testable): row `{itemId:"__run__", kind:"action", provider:"run-command", value:<full command>, label: analysis.firstLine + (extraLines? "  (+N lines)" : ""), detail: "Enter: run in terminal · Shift+Enter: run here", iconName:"utilities-terminal", isDanger roles via `section:"danger"` when dangerous}`; history rows `{itemId:"__hist_<i>__", provider:"run-command", value:<cmd>, label:<cmd>, detail:"History", iconName:"document-open-recent"}`.
- Placement: Strong → rows prepended; Weak → appended after local matches; `>` prefix → command = text after `>`, always shown (empty → up to 10 history rows).
- `root.armedRunId: string` — first Enter on a dangerous row sets it and relabels "Press Enter again to run (dangerous)"; any other key clears it.
- Enter on run row → `Shell.runCommandInTerminal(value)`, close menu. Shift+Enter → `Shell.runCommandInline(value)`; if it returns true → show output panel (Task 6), else close (routed to terminal).

- [ ] **Step 1: Failing QML tests** `tst_runrows.qml`: `strongPrepended`, `weakAppended`, `bareWordNoRow` (analysis.signal 0, no `>` → no run row), `prefixForcesRow`, `multiLineLabel` (`"a\nb\nc"` → label `"a  (+2 lines)"`), `historyRowsForBarePrefix`.
- [ ] **Step 2: Run, expect FAIL.** **Step 3: Implement** `MenuModel.commandRows`, wire into `rebuildDisplayInner`, keys, arming. **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(run): run-command rows, Shift+Enter inline, danger arming, history rows"`.

### Task 6: Inline output panel

**Files:** Create `CommandOutputPanel.qml`; modify `Menu.qml` (instantiate inside `card`, visible when `root.outputPanelOpen`).

**Interfaces:** `CommandOutputPanel { runner: Shell.runner }` — header: command first line + state (`Running…` / `Exit N`), monospace `TextArea` (read-only, selectable, auto-scroll to end unless the user scrolled up), buttons `Stop` (enabled while running), `Copy output` (`Shell.setClipboard(runner.output)`), `Close`. Esc closes the panel (not the menu) and returns to the list; the panel does not stop the process.

- [ ] **Step 1: Failing QML test** `tst_outputpanel.qml` with a fake runner object (`QtObject { property bool running; property string output; property string command; property int exitCode; function stop(){stopped=true} }`): `showsRunningState`, `stopCallsRunner`, `copyUsesOutput` (spy via a `copyRequested(string)` signal on the panel).
- [ ] **Step 2: Run, expect FAIL.** **Step 3: Implement.** **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(run): inline command output panel"`.

### Task 7: Mirror, full verification, deploy

- [ ] **Step 1:** Sync per Global Constraints; `diff -rq` src/tests and `diff -q` each QML/JS file.
- [ ] **Step 2:** Build both repos — no `error|warning`.
- [ ] **Step 3:** `tests/cpp/cmdtests` all pass; `qmltestrunner -input tests/qml` all pass; `./tests/run_e2e_tests.sh` → 228/228 (f20 memory < 50 MB).
- [ ] **Step 4:** Deploy (stop, copy, start, `is-active`), then regression probe: `Shell` terminal argv for `echo $(echo X)` verified by Task 1 test; live check that `--show`/`--hide` log no QML errors.
