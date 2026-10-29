# 🦅 RavenSpot

> **Lightning-fast, keyboard-driven Spotlight Search, Command Palette & Application Menu for Linux (KDE Plasma 6 / CachyOS) and Windows 11.**

RavenSpot brings the speed and elegance of macOS Spotlight and Raycast combined with the clean minimalism of Omarchy to both **Linux (KDE Plasma 6 on Wayland)** and **Windows 11** using `PySide6`, `QtQuick`, and native OS shell integrations.

---

## ✨ Features

- ⚡ **Instant Fuzzy Search**: Search applications, settings, actions, and custom triggers in milliseconds.
- 🗂️ **Categorized Apps**: Categorized FreeDesktop applications (Linux) & Start Menu shortcuts (Windows) with live badge counts.
- 🍬 **Candy Icon Theme**: Bundled gradient Candy icons for native vibrancy and crisp scaling across platforms.
- 🖱️ **Rich Context Menu (Right-Click)**:
  - **Linux**: Launch, Desktop Actions, Open containing folder in Dolphin, Run in Konsole, Open in Discover, Edit `.desktop`, Move to Trash.
  - **Windows**: Launch, Show in File Explorer (`/select,`), Open in Windows Terminal / PowerShell, Edit shortcut in Notepad, Move to Recycle Bin (with full Undo support via Win32 Shell API).
- 🧮 **Inline Calculator**: Type any math expression (`125 * 4`, `500 / 2.5`, `(20 + 5) * 4`, `2^8`) and press Enter to copy the evaluated result to your clipboard.
- 💻 **Terminal Command Mode**: Prefix queries with `>` to run any shell command directly in Konsole or Windows Terminal / PowerShell (`> btop`, `> ping 1.1.1.1`).
- 🌐 **Web Fallback Search**: Instant Google and DuckDuckGo search fallback when no local match is found (or prefix queries with `?`).
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

#### Requirements
- Linux with KDE Plasma 6 (Wayland session)
- Python 3 with `PySide6`
- `layer-shell-qt` (or `layer-shell-qt6`)
- Nerd Font (e.g., `MesloLGS Nerd Font`, `JetBrainsMono Nerd Font`)

#### Quick Install
```bash
# 1. Clone the repository
git clone https://github.com/cdokuro/ravenspot.git ~/.local/share/ravenspot

# 2. Link executable wrapper
mkdir -p ~/.local/bin
ln -sf ~/.local/share/ravenspot/bridge.py ~/.local/bin/ravenspot
chmod +x ~/.local/share/ravenspot/bridge.py

# 3. Install desktop launcher and global shortcut
cp ~/.local/share/ravenspot/ravenspot.desktop ~/.local/share/applications/
kbuildsycoca6 --noincremental

# 4. Bind Meta+Space in KDE Shortcuts (if not automatic)
# System Settings -> Shortcuts -> RavenSpot -> Launch: Meta+Space
```

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
