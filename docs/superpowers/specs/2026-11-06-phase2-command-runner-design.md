# Phase 2 Design: Paste-and-run command runner

**Date:** 2026-11-06 · **Size:** project (one feature, several parts)

## Intent
The user wants to copy any command (from a browser, chat, docs), paste it into
RavenSpot and run it directly — "everything on the PC from one place". The user
delegated planning; decisions below are my calls unless marked otherwise.

## Current state (evidence)
- `>` prefix builds a string `konsole -e bash -c "<cmd>; exec bash"` escaping
  only `"`, then runs it with `/bin/sh -c`. Verified: `$$`, `$(...)`, backticks
  and `$VAR` expand in the **daemon's** outer shell before the terminal opens
  (e.g. `> sudo pacman -Rns $(pacman -Qdtq)` runs the inner query in the
  background, not in the terminal).
- Without `>`, a pasted command is only matched as search text.
- `TextInput` keeps pasted newlines (`"a\nb"`), so multi-line pastes arrive
  intact.
- `ShellBridge::runInTerminal(cmd)` already passes argv correctly
  (`term -e /bin/sh -c cmd`) and picks konsole/kitty/alacritty/foot/wezterm.

## Goals
1. Run pasted/typed commands without the `>` prefix when the text is clearly a
   command; `>` still forces command mode.
2. Correct execution: the command reaches bash as one argv element; nothing is
   expanded by RavenSpot.
3. Two ways to run:
   - **Enter → terminal** (Konsole, stays open afterwards in the user's shell).
   - **Shift+Enter → inline**: runs in the background, output streams into a
     panel inside RavenSpot (stdout+stderr, exit code, Stop, Copy output).
4. `sudo …` and interactive programs (htop, btop, vim, nano, less, ssh, top,
   man, journalctl -f …) always go to the terminal, even with Shift+Enter.
5. Multi-line pastes run as one bash script; the row shows the first line and
   "+N lines".
6. History: last 50 run commands, shown when the query is `>`; history entries
   matching the query appear as rows.
7. Danger guard: obviously destructive commands need a second Enter.

## Non-goals
- No embedded terminal emulator (interactive input inside RavenSpot).
- No per-command environment/working-directory editor (runs in `$HOME`).
- No remote/SSH execution features.

## Anti-goals
- Hijacking normal search: typing `firefox` must still launch the app first.
- Silent destructive runs.
- Pushing the daemon past the 50 MB private-memory E2E limit (now ~47 MB).

## Approaches
1. **Terminal-only.** Fix quoting, add auto-detect + history; every run opens
   Konsole. Smallest, but no output inside RavenSpot.
2. **Terminal + inline output panel (chosen).** Same as 1, plus a background
   QProcess runner whose output streams into a QML panel. Interactive/sudo
   still route to the terminal. Fits "do everything here" without a heavy
   dependency.
3. **Embedded terminal (qmltermwidget/KPty).** Full interactivity inside the
   card; new dependency, large memory cost near the 50 MB limit, Wayland
   focus/keyboard complexity. Rejected.

## Decisions (my calls)
- **Detection** (no `>`): a "Run command" row appears when the text contains
  shell syntax (`|`, `&&`, `||`, `;`, `>`, `<`, `$(`, backtick, newline, or a
  leading `sudo`/`./`/`~/`/`/`), or when the first token is an executable on
  `PATH` and is followed by arguments. Strong signals (sudo, newline, pipe,
  `&&`) put the row first; otherwise it goes after app/settings matches.
  A bare single word never produces a run row without `>`.
- **Terminal run:** `term -e bash -c '<cmd>\nprintf "\n[exit %s] " $?; exec "${SHELL:-bash}" -i'`
  built as argv (no string concatenation). bash runs the command because pasted
  snippets are bash syntax; the user's shell (fish) takes over afterwards.
- **Inline run:** `bash -c '<cmd>'` via QProcess in `$HOME`, merged channels,
  UTF-8, output capped at 2000 lines (oldest dropped), Stop = SIGTERM then
  SIGKILL after 2 s. One inline run at a time; menu stays open while running;
  closing the menu keeps the process running and a notification reports the
  exit code when it finishes.
- **Danger list:** `rm -rf /`, `rm -rf ~`/`$HOME`, `rm -rf /*`, `mkfs`,
  `dd … of=/dev/`, `> /dev/sd*`/`nvme*`, `:(){`, `chmod -R 777 /`,
  `chown -R … /`. First Enter turns the row red "Press Enter again to run";
  any other key resets.
- **History file:** `~/.local/share/ravenspot/command_history.json`, newest
  first, de-duplicated, max 50; written after a run is started.
- Row labels/strings in English (user preference for UI text).

## Left to the builder
- Exact QML layout of the output panel (inside the card, replacing the list
  while visible; Esc returns to the list).
- Interactive-program list beyond the names above.

## Verification
- C++ unit-style tests for detection, danger matching and argv building
  (new QtTest executable under `tests/cpp/`), QML tests for row building.
- Regression: the `$(...)`/`$$` case reaches bash unexpanded.
- E2E 228/228; memory test stays under 50 MB with the panel idle.
- Manual: paste multi-line script, Shift+Enter `ls -la /etc | head`, Stop on
  `sleep 30`, `sudo pacman -Syu` opens Konsole, `rm -rf ~` asks twice while
  `rm -rf ~/tmp/x` does not.
