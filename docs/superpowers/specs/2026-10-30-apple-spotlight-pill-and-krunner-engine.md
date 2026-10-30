# Design Spec: RavenSpot Apple-Style Search Pill & KRunner Action Engine

**Date:** 2026-10-30  
**Target Version:** 0.2.2  
**Platform:** Linux (KDE Plasma 6 / Wayland) & Windows 11  

---

## 1. Intent & Motivation

Transform RavenSpot's flat text search input into an **Apple-style Spotlight search capsule (pill container)** that delivers an elegant, high-contrast, frosted-glass aesthetic reminiscent of macOS Spotlight and Raycast. Simultaneously, expand the engine with **KRunner-style instant multi-action capabilities**, allowing users to perform system tasks, advanced math, unit conversion, directory navigation, web bangs, and process management directly from a single, blisteringly fast prompt (<10ms latency).

---

## 2. Visual Architecture: Apple Spotlight Search Pill

### 2.1 The Pill Container (`searchPill`)
- **Dimensions & Geometry:** Height 44px, rounded corners (`radius: 12px`), placed in the search header with comfortable margins (`leftMargin: 16`, `rightMargin: 16`).
- **Surface & Glass Effect:** A dedicated background pill with subtle contrast:
  - Base: `Qt.rgba(255, 255, 255, 0.08)` on dark themes (or `Qt.rgba(0, 0, 0, 0.06)` on light themes).
  - Border: 1px subtle outline (`Qt.rgba(255, 255, 255, 0.12)` default, transitions to `root.accentColor` with 0.6 opacity when `searchInput.activeFocus`).
- **Left Search Icon:**
  - An elegant magnifying glass glyph (`󰍉` or vector search) in `root.fgDim`, glowing gently when the input has text.
- **Breadcrumb Tag (`categoryBadge`):**
  - When drilling down into submenus (e.g. `Apps > Development`), a compact pill tag appears (`‹ Geliştirme`) on the left. Clicking it returns to the root menu immediately.
- **Input & Placeholder:**
  - `TextInput` vertically centered with 15px font size.
  - Placeholder: `"RavenSpot'ta ara veya bir komut yaz..."` visible when `filterText` is empty and at root.
- **Right Action Indicators:**
  - Dynamic clear button `(×)`: Circular hoverable icon when `filterText.length > 0`. Clicking it clears the text.
  - Shortcut badge: A discreet `Esc` badge indicator when empty.

---

## 3. KRunner-Style Multi-Action Capabilities

All runners are executed in-memory with priority scoring to guarantee immediate (<10ms) responses:

### 3.1 System Power & Session Controls
Keywords supported in Turkish and English:
- **Kapat / Shutdown / Poweroff:** Triggers `systemctl poweroff` (or `org.kde.Shutdown.logoutAndShutdown()`).
- **Yeniden Başlat / Reboot / Restart:** Triggers `systemctl reboot` (or `org.kde.Shutdown.logoutAndReboot()`).
- **Kilitle / Lock / Lock Screen:** Triggers `loginctl lock-session` (or `org.kde.kscreenlocker.lock()`).
- **Uyut / Sleep / Suspend:** Triggers `systemctl suspend`.
- **Çıkış / Logout:** Triggers `org.kde.Shutdown.logout()`.
- **Plasma Yeniden Başlat / Restart Plasma:** Restarts `plasmashell`.

### 3.2 Advanced Math & Unit/Currency Conversions
- Extended math expressions: `sqrt(x)`, `sin(x)`, `cos(x)`, `pi`, `abs(x)`, `^` exponentiation, and percentages (`20% of 150`, `250 + 10%`).
- Unit conversions:
  - Length: `km` to `m`, `m` to `km`, `miles` to `km`, `km` to `miles`.
  - Data: `gb` to `mb`, `mb` to `gb`, `tb` to `gb`.
  - Temperature: `C` to `F`, `F` to `C`.
  - Mass: `kg` to `lbs`, `lbs` to `kg`, `kg` to `g`.
- Enter copies the result immediately to the clipboard with audio confirmation.

### 3.3 Path & Directory Navigator
- Entering paths starting with `~` or `/` (e.g. `~/Downloads`, `/etc`, `~/Belgeler`):
  - Action 1: Open directory in Dolphin (`xdg-open <path>`).
  - Action 2: Open terminal in directory (`konsole --workdir <path>`).

### 3.4 Web Search Bangs
Instant query dispatching:
- `g <query>`: Google
- `yt <query>`: YouTube
- `gh <query>`: GitHub
- `w <query>`: Wikipedia
- `tr <query>`: Google Translate (TR <-> EN)
- `map <query>`: Google Maps
- `ddg <query>`: DuckDuckGo

### 3.5 Process Management (`kill <name>`)
- Typing `kill <name>` scans running processes via `Shell.listProcesses(filter)` and presents matching processes with PID, memory/CPU, and single-click termination.

### 3.6 Quick Audio & Display Commands
- `ses <val>` or `vol <val>`: Immediately sets system volume to `<val>%`.
- `sessiz` or `mute`: Toggles mute.

---

## 4. Verification & Success Criteria

1. **Visual:**
   - Search bar renders inside the rounded pill container with polished glass aesthetics.
   - Dynamic focus border, magnifying glass icon, clear button, and breadcrumb navigation function properly.
   - Titlebar drag handle and top audio/telemetry indicators remain functional and well-aligned.
2. **Behavioral:**
   - All KRunner runners trigger accurately and instantly.
   - Math and unit conversions evaluate correctly.
   - System power actions execute appropriate platform commands.
3. **Release:**
   - Version updated to `0.2.2`.
   - Committed and tagged as `v0.2.2` on GitHub.
   - `ravenspot.service` active and operational.
