# 🦅 RavenSpot

> **Lightning-fast, keyboard-driven Spotlight Search, Command Palette & Application Menu for Linux (KDE Plasma 6 / CachyOS) and Windows 11.**

RavenSpot brings the speed and elegance of macOS Spotlight and Raycast combined with the clean minimalism of Omarchy to both **Linux (KDE Plasma 6 on Wayland)** and **Windows 11** using `PySide6`, `QtQuick`, and native OS shell integrations.

---

## ✨ Features

- 🍎 **Apple Spotlight Frosted Search Capsule**: Floating rounded pill container (`radius: 12px`) with frosted glass backdrop, dynamic focus glowing borders, magnifying glass icon, category breadcrumbs, clear button `(×)`, and `ESC` badge.
- ⚡ **KRunner-Style Multi-Action Engine**:
  - **System Power & Session Controls**: Instant system actions for `kapat` / `shutdown`, `yeniden başlat` / `reboot`, `kilitle` / `lock`, `uyut` / `sleep`, `çıkış` / `logout`, and `plasma restart`.
  - **Inline Math & Unit Conversion**: Compute expressions (`sqrt(144)`, `2^10`, `20% of 150`, `150 + 10%`, `pi * 2`) and convert units (`10 km in m`, `50 gb in mb`, `100 c in f`, `5 kg in lbs`, `10 miles in km`) with instant clipboard copy.
  - **Web Search Bangs**: Query directly with prefixes like `g ` (Google), `yt ` (YouTube), `gh ` (GitHub), `w ` (Wikipedia), `tr ` (Translate), `map ` (Maps), and `ddg ` (DuckDuckGo).
  - **Process Killer**: Type `kill <name>` to scan running processes and terminate matching PIDs with one keystroke.
  - **Path & Directory Navigator**: Enter paths starting with `~` or `/` (`~/Downloads`, `/etc`) to open directly in Dolphin or Konsole.
  - **Audio & Volume Commands**: Instant volume control with `ses 70`, `vol 50`, or `mute`/`sessiz`.
- 🗲 **System Power 2-Column Grid**: Opening the `System` category displays an ergonomic 2-column card grid for power actions: Reboot, Shutdown, Lock, Logout, Suspend, and Screensaver with arrow key 2D navigation.
- 🔄 **Live CachyOS / Arch Update Badge**: Status badge in the header shows live pending update counts (red badge) or up-to-date status (green badge) with single-click interactive terminal updater.
- ⚡ **Instant Fuzzy Search**: Search applications, settings, actions, and custom triggers in milliseconds.
- 🗂️ **Categorized Apps**: Categorized FreeDesktop applications (Linux) & Start Menu shortcuts (Windows) with live badge counts.
- 🍬 **Candy Icon Theme**: Bundled gradient Candy icons for native vibrancy and crisp scaling across platforms.
- 🖱️ **Rich Context Menu (Right-Click)**:
  - **Linux**: Launch, Desktop Actions, Open containing folder in Dolphin, Run in Konsole, Open in Discover, Edit `.desktop`, Move to Trash.
  - **Windows**: Launch, Show in File Explorer (`/select,`), Open in Windows Terminal / PowerShell, Edit shortcut in Notepad, Move to Recycle Bin (with full Undo support via Win32 Shell API).
- 💻 **Terminal Command Mode**: Prefix queries with `>` to run any shell command directly in Konsole or Windows Terminal / PowerShell (`> btop`, `> ping 1.1.1.1`).
- 📋 **Integrated Clipboard Manager**: Live list of your recent clipboard history right inside the palette with single-click restore.
- 🎨 **Theme Manager & Favorites**: Browse themes with currently active theme highlighted and single-key (`F`) favoriting.
- 📊 **Realtime Telemetry**: Live CPU, RAM, and Disk usage metrics.
- 🪟 **Single-Instance IPC & Global Hotkey**:
  - **Linux**: Wayland `LayerShellQt` overlay toggled via `Meta + Space`.
  - **Windows**: Win32 native global hotkey (`Alt + Space` or `Win + Space`) plus `QLocalServer` single-instance IPC.

---

## 🚀 Keybindings & Shortcuts

| Key | Platform | Action |
|---|---|---|
| **`Meta`** (Alone) | Linux (KDE) | **Open / Toggle RavenSpot** |
| **`Meta + Space`** | Linux (KDE) | **Open / Toggle RavenSpot** |
| **`Alt + Space` / `Win + Space`** | Windows 11 | **Open / Toggle RavenSpot** |
| **`Right-Click`** | All | Context menu for applications and files |
| **`Type text`** | All | Instant fuzzy search across all items and apps |
| **`Enter` / `→`** | All | Launch selected item or enter submenu |
| **`Backspace` / `Esc`** | All | Navigate up one level (closes menu if at root) |
| **`↑` / `↓`** | All | Select previous / next item |
| **`PgUp` / `PgDn`** | All | Fast scroll (jump 6 items) |
| **`F`** | All | Toggle favorite on selected color scheme |
| **`>` + command** | All | Run command in terminal (e.g. `> btop`, `> winget upgrade`) |
| **`?` + query** | All | Quick web search in browser |

---

## 📦 Installation & Setup

### 🪟 Windows 11

#### Option A: Prebuilt Executable (Recommended)
1. Go to the **[Releases](https://github.com/cdokuro/ravenspot/releases)** page or **GitHub Actions Artifacts**.
2. Download `RavenSpot-Windows-x64.zip` and extract it anywhere (e.g. `C:\Tools\RavenSpot`).
3. Run `RavenSpot.exe` (or add a shortcut in your `shell:startup` folder to launch on boot).
4. Press **`Alt + Space`** (or `Win + Space`) anytime to toggle RavenSpot!

#### Option B: Build from Source with `build.bat`
```cmd
# 1. Clone repository
git clone https://github.com/cdokuro/ravenspot.git
cd ravenspot

# 2. Double click or run build.bat
build.bat

# The standalone RavenSpot.exe will be ready in dist\RavenSpot.exe
```

---

### 🐧 Linux (KDE Plasma 6 / CachyOS)

#### Quick Install (Automated & Interactive)
Run the automated installer script:
```bash
git clone https://github.com/cdokuro/ravenspot.git
cd ravenspot
./install.sh
```
The installer will set up RavenSpot, configure the resident systemd daemon, map the solitary `Meta` key, and interactively ask if you'd like to install the optional telemetry tools:
- **`lact`**: Advanced AMD Radeon GPU (RX 7000/9000 series, RX 9070 XT) clocks, VRAM/Hotspot thermals, and fan curves.
- **`cpu-x`**: Comprehensive CPU & DDR5 RAM timings, SPD details, and benchmarks (CPU-Z alternative for Linux).
- **`mission-center`**: Modern Windows 11 Performance Tab interface.
- **`amdgpu_top`**: Live real-time AMD GPU compute engine telemetry.

---

## 🔧 Customization

You can override or extend the menu without touching core files:
- **Linux**: `~/.config/ravenspot/menu.jsonc`
- **Windows**: `%APPDATA%\ravenspot\menu.jsonc`

```jsonc
{
  "my_custom_script": {
    "icon": "🚀",
    "label": "My Custom Script",
    "action": "~/scripts/backup.sh"
  }
}
```

---

## 📄 License

Distributed under the [MIT License](LICENSE).  
Copyright (c) 2026 cdokuro (Dokuro Raven).
