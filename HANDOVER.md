# Project Handover Notes: RavenSpot (Native C++20 / Qt 6)

> [!IMPORTANT]
> **To the Incoming AI Assistant:**
> This repository is a performance-critical C++20 / Qt 6 native spotlight search launcher and resident desktop daemon designed specifically for **KDE Plasma 6 on Wayland (Arch/CachyOS)**. It replaces a legacy Python/PySide prototype with sub-millisecond hotkey response and ~30MB idle footprint.

---

## 1. Project Locations & Repositories

| Item | Path / URL |
| :--- | :--- |
| **Primary Git Repository** | `/home/dokuro/Masaüstü/ravenspot` |
| **Workspace Mirror** | `/home/dokuro/Masaüstü/omarchyapp` |
| **Remote Git Origin** | `https://github.com/cdokuro/ravenspot.git` (`main` branch) |
| **Installed User Binary** | `/home/dokuro/.local/bin/ravenspot` |
| **Systemd Service** | `~/.config/systemd/user/ravenspot.service` (`ravenspot.service`) |
| **IPC Daemon Socket** | `/run/user/1000/ravenspot.sock` |

> [!WARNING]
> Both `/home/dokuro/Masaüstü/ravenspot` and `/home/dokuro/Masaüstü/omarchyapp` **must remain in sync**. Whenever code or QML files are edited, mirror them to both directories, rebuild both, and push commits to `cdokuro/ravenspot`.

---

## 2. System & Hardware Specifications

- **OS:** Linux Arch / CachyOS (x86_64)
- **Desktop Environment:** KDE Plasma 6.8.2 on Wayland (`kwin_wayland`)
- **Display Setup:**
  - Primary: `DP-1` (2560x1440 @ 144Hz)
  - Secondary: `DP-2` (1920x1080 @ 165Hz)
- **Input Hardware:** Pixart SC900MAX 8K Dongle (**8000Hz gaming mouse**). High polling rates (8000 events/sec) require strict avoidance of synchronous IPC and quadratic coordinate accumulations.

---

## 3. High-Level Architecture

```
[Super+Space Global Hotkey]
           │
           ▼
[ravenspot --toggle] (CLI Client)
           │
           ▼ (UNIX Socket: /run/user/1000/ravenspot.sock)
[DaemonServer] (Resident Daemon, PID in systemd)
           │
           ├──▶ [LayerShellWindow] (C++20 - LayerShellQt overlay surface)
           │          ├── QQuickWindow (543x648px, transparent background)
           │          ├── zwlr_layer_surface_v1 (AnchorTop | AnchorLeft)
           │          ├── Blur & Contrast via KF6WindowSystem (KWindowEffects)
           │          └── V-Sync Locked Drag Handling (setMargins + requestUpdate)
           │
           ├──▶ [ShellBridge] (QML C++ Backend)
           │          ├── AppIndexer (Multi-tier app launch: gtk-launch / kioclient / exec)
           │          ├── AudioEngine (Velvety creamy click acoustics, default 10% volume)
           │          ├── ThemeManager (themes.json + candy-icons FreeDesktop theme)
           │          ├── KRunnerActions (D-Bus power actions: Sleep, Restart, Shutdown)
           │          └── CalcConverter (Live math expressions & unit conversions)
           │
           └──▶ [Menu.qml] (Qt Quick Scene Graph UI)
                      ├── 495px Card inside 543px Window (24px glow margin)
                      ├── Title bar MouseArea for live window dragging
                      ├── Dependency prompt modals (CPU-X, GPU utilities, Nashi)
                      └── System stats & power profile flyout
```

---

## 4. Critical Wayland & LayerShell Invariants

> [!CAUTION]
> Deviating from these invariants will break window movement, cause 1500px teleportation jumps, or freeze the surface!

1. **Double-Buffered Margin Commits (`requestUpdate` required):**
   - Layer surfaces (`zwlr_layer_surface_v1`) are positioned with `set_margin(top, right, bottom, left)`.
   - Margin calls are **pending state**. The compositor (KWin) only applies margins upon `wl_surface.commit`.
   - In Qt Quick, a commit only happens when a frame renders. Always call `m_window->requestUpdate()` immediately after `m_layerWindow->setMargins(...)`.
2. **Local vs. Global Coordinates (The Feedback Trap):**
   - Wayland clients have NO global cursor position (`QCursor::pos()` returns `(0, 0)`).
   - Mouse events in QML `MouseArea` are surface-local coordinates.
   - **NEVER subtract applied deltas from `lastX`** (`lastX -= moved.x` is forbidden). Because Wayland commits are asynchronous, subtracting applied moves on sub-frame events induces an $\mathcal{O}(N^2)$ runaway feedback loop, teleporting the window across screens.
   - Use pure incremental deltas:
     ```qml
     var dx = mouse.x - lastX
     var dy = mouse.y - lastY
     lastX = mouse.x
     lastY = mouse.y
     Shell.dragBy(dx, dy)
     ```
3. **No Artificial Software Timers for Dragging:**
   - Do NOT use arbitrary 7ms software timers. They beat against the monitor's 6.94ms V-Sync (beat frequency), causing skipped frames and jitter.
   - Directly call `m_layerWindow->setMargins(...)` and `m_window->requestUpdate()`. Qt Quick automatically coalesces all sub-frame calls into exactly one frame per V-Sync at native 144Hz.

---

## 5. Build, Test & Deployment Workflow

### Compile Both Workspaces
```bash
# 1. Build ravenspot
cd /home/dokuro/Masaüstü/ravenspot
make -j$(nproc)

# 2. Build omarchyapp mirror
cd /home/dokuro/Masaüstü/omarchyapp
make -j$(nproc)
```

### Run Full Test Suite (228 Automated E2E Tests)
```bash
cd /home/dokuro/Masaüstü/ravenspot
./tests/run_e2e_tests.sh
```
*Note: All 228 tests across Tier 1 (Features), Tier 2 (Boundaries), Tier 3 (Pairwise Interactions), and Tier 4 (Workloads) must pass with 0 failures.*

### Deploy & Restart Daemon
```bash
systemctl --user stop ravenspot.service
cp /home/dokuro/Masaüstü/ravenspot/ravenspot ~/.local/bin/ravenspot
systemctl --user start ravenspot.service
systemctl --user status ravenspot.service --no-pager
```

### Git Push
```bash
cd /home/dokuro/Masaüstü/ravenspot
git add <files>
git commit -m "feat/fix: <description>"
git push origin main
```

---

## 6. Recent Work Completed

1. **Application Launching (`AppIndexer`):**
   - Fixed window hijacking bug where activating existing windows falsely captured matching names.
   - Added robust 3-tier execution: Tier 1 `gtk-launch <desktop-id>`, Tier 2 `kioclient exec file://<desktop-path>`, Tier 3 direct sanitized executable execution.
2. **Audio & Volume Defaults:**
   - Default app volume set to 10% (was 70%).
   - Velvety creamy keyboard acoustics enabled with iOS-style soft haptics.
3. **Window Geometry & Modals:**
   - Expanded window card width by 10% (card: 495px, window: 543px with 24px halo padding).
   - Added dependency check modals with package manager commands for CPU-X and GPU management tools.
4. **Wayland Drag-to-Move:**
   - Title bar MouseArea allows dragging the window smoothly across screens at 144Hz.
   - Eliminated quadratic runaway loop and 7ms timer beat frequency.
5. **Knowledge Persistence (`/learn`):**
   - Created workspace rule `.agents/rules/wayland-input-handling.md` in both repositories.

---

## 7. Key Files Cheatsheet

- `src/main.cpp`: Daemon initialization, CLI argument parser, single-instance socket server.
- `src/core/LayerShellWindow.hpp / .cpp`: Wayland layer-shell surface, centering, blur effect, dragging geometry.
- `src/ui/ShellBridge.hpp / .cpp`: Q_INVOKABLE methods callable from QML (`dragBy`, `beginDrag`, `endDrag`, `launchApp`, etc.).
- `src/search/AppIndexer.hpp / .cpp`: XDG `.desktop` file scanner and multi-tier launcher.
- `Menu.qml`: User interface, search bar, list views, theme bindings, title bar mouse area.
- `.agents/rules/wayland-input-handling.md`: Wayland double-buffering and high-polling mouse invariants.
