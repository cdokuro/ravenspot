# Spotlight Floating Window Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform RavenSpot from a single-monitor-anchored full-screen LayerShell overlay with a dark scrim into a true, native, floating Spotlight search window that glides smoothly pixel-by-pixel across multiple monitors.

**Architecture:**
- Remove the full-screen `LayerShell` surface and the full-screen `scrim` rectangle.
- Size `Window` (`root`) to the exact card dimensions plus a 24px transparent border for drop shadow/glow (`width: cardWidth + 48`, `height: cardHeight + 48`).
- Configure window flags: `Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint`.
- Center the window on the active screen / mouse cursor on startup using KWin activeOutput and Screen geometry.
- Bind `startSystemMove()` to `Alt` and `Meta` modifier mouse press events so KWin natively moves the window across monitors with sub-pixel Wayland physics.
- Handle clicking outside via `onActiveChanged: if (!active) closeMenu()`.

**Tech Stack:** Qt 6.12, PySide6, QML, KWin Wayland, xdg_toplevel.

---

### Task 1: Refactor `Menu.qml` Window & Card Geometry
- Remove `org.kde.layershell` import and `LayerShell.Window.*` properties.
- Update `Window` geometry:
  - `width: card.width + 48`
  - `height: card.height + 48`
  - `color: "transparent"`
  - `flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint`
- Remove `scrim` Rectangle.
- Center `card` inside `root` (`anchors.centerIn: parent`).
- Position the `root` window in the center of the active monitor on launch.

### Task 2: Implement Native Window Movement (`startSystemMove`) & Modifiers
- Add a drag trigger in `card`:
  - When `mouse.modifiers & (Qt.AltModifier | Qt.MetaModifier)`: call `root.startSystemMove()`.
- Add `onActiveChanged`:
  - When `!root.active`: call `closeMenu()`.

### Task 3: Python Bridge Window Centering & Activation
- In `bridge.py`:
  - Position window in center of active screen on launch and reopen.
  - Test and verify across both monitors (DP-1 2560x1440 and DP-2 1920x1080).
- Sync updated files and commit to git repo.
