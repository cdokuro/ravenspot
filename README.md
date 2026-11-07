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
  - **Windows**: Launch, Show in File Explorer (`/select,`), Open in Windows Terminal / PowerShell, Shortcut properties, Move to Recycle Bin.
- 💻 **Terminal Command Mode**: Prefix queries with `>` to run any shell command directly in Konsole or Windows Terminal / PowerShell (`> btop`, `> ping 1.1.1.1`).
- 🔗 **Paste a Link, Pick an Action**: Paste an `http(s)://` or `www.` link and press `Enter`: RavenSpot asks whether to **open it in your default browser** or **download the video** with [yt-dlp](https://github.com/yt-dlp/yt-dlp) into your Downloads folder (live progress in the output panel; offers to install yt-dlp if it is missing).
- 📋 **Integrated Clipboard Manager**: Live list of your recent clipboard history right inside the palette with single-click restore.
- 🎨 **Theme Manager & Favorites**: Browse themes with currently active theme highlighted and single-key (`F`) favoriting.
- 📊 **Realtime Telemetry**: Live CPU, RAM, and Disk usage metrics.
- 🪟 **Single-Instance IPC & Global Hotkey**:
  - **Linux**: Wayland `LayerShellQt` overlay toggled via `Meta + Space`.
  - **Windows**: Win32 native global hotkey (`Alt + Space`, configurable with `RAVENSPOT_HOTKEY`) plus named-pipe single-instance IPC.

---

## 🚀 Keybindings & Shortcuts

| Key | Platform | Action |
|---|---|---|
| **`Meta`** (Alone) | Linux (KDE) | **Open / Toggle RavenSpot** |
| **`Meta + Space`** | Linux (KDE) | **Open / Toggle RavenSpot** |
| **`Alt + Space`** | Windows 11 | **Open / Toggle RavenSpot** |
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

#### Option A: Prebuilt Build (Recommended)
1. Go to the **[Releases](https://github.com/cdokuro/ravenspot/releases)** page or the **GitHub Actions artifacts** of the *Build Windows* workflow.
2. Download `RavenSpot-Windows-x64.zip` and extract it anywhere (e.g. `C:\Tools\RavenSpot`). Keep the folder together: Qt DLLs, `Menu.qml`, `icons/`, `sounds/` and `compat/` sit next to the exe.
3. Run `ravenspot.exe --daemon` (or put a shortcut to it in your `shell:startup` folder to launch on boot).
4. Press **`Alt + Space`** anytime to toggle RavenSpot. If another program already owns it, `Ctrl + Alt + Space` is used instead; set `RAVENSPOT_HOTKEY` (e.g. `win+shift+space`) to choose your own.

The Windows build is the same native C++ app as on Linux. Windows-specific parts: Start Menu and Store apps, Explorer, Windows Terminal / PowerShell, Win32 power and volume, and Job objects for inline commands. Optional: `RAVENSPOT_WIN_BACKDROP=acrylic` (or `mica`) adds a DWM backdrop on Windows 11.

#### Option B: Build from Source (Qt 6 + MSVC)
```cmd
:: Qt 6.8+ (msvc2022_64, with the Qt Multimedia module) and Visual Studio 2022
git clone https://github.com/cdokuro/ravenspot.git
cd ravenspot
mkdir build && cd build
qmake ..\ravenspot.pro "CONFIG+=release" "CONFIG-=debug_and_release"
nmake
windeployqt --release --qmldir .. ravenspot.exe
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
- **Windows**: `%LOCALAPPDATA%\ravenspot\menu.jsonc`

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
