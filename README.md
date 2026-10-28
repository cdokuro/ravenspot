# 🦅 RavenSpot

> **Lightning-fast, keyboard-driven Spotlight Search, Command Palette & Application Menu for KDE Plasma / Linux.**

RavenSpot brings the speed of macOS Spotlight and Raycast combined with the clean minimalism of Omarchy to KDE Plasma on Wayland using `LayerShellQt`, `PySide6`, and `QtQuick`.

---

## ✨ Features

- ⚡ **Instant Fuzzy Search**: Search applications, settings, actions, and custom triggers in milliseconds.
- 🗂️ **Categorized Apps**: Categorized FreeDesktop applications with live badge counts (Games, Internet, Dev, Media, Graphics, Office, System, Utilities).
- 🧮 **Inline Calculator**: Type any math expression (`125 * 4`, `500 / 2.5`, `(20 + 5) * 4`) and press Enter to copy the result to your clipboard.
- 💻 **Terminal Command Mode**: Prefix queries with `>` to run any bash command directly in Konsole (`> btop`, `> ping 1.1.1.1`).
- 🌐 **Web Fallback Search**: Instant Google and DuckDuckGo search fallback when no local match is found (or prefix with `?`).
- 📋 **Integrated Clipboard Manager**: Live list of your recent clipboard history right in the palette with single-click restore.
- 🎨 **Theme Manager & Favorites**: List installed color schemes with currently active theme highlighted and single-key (`F`) favoriting.
- 🍐 **Nashi Radial Toolkit Integration**: Direct drill-down to conversion, tool, and action wheels.
- 🪟 **Single-Instance IPC**: Clean toggle behavior with zero stacked windows; smooth fade & scale transitions.
- ⌨️ **Native Wayland Overlay**: Built on KDE's `LayerShellQt` (`zwlr_layer_shell_v1`) with exclusive keyboard focus.

---

## 🚀 Keybindings & Shortcuts

| Key | Action |
|---|---|
| **`Meta` (Super)** | Open / Toggle RavenSpot |
| **`Type text`** | Instant fuzzy search across all items and apps |
| **`Enter` / `→`** | Launch selected item or enter submenu |
| **`Backspace` / `Esc`** | Navigate up one level (closes if at root) |
| **`↑` / `↓`** | Select previous / next item |
| **`PgUp` / `PgDn`** | Scroll fast (jump 6 items) |
| **`F`** | Toggle favorite on selected color scheme |
| **`>` + command** | Run command in terminal (e.g. `> htop`) |
| **`?` + query** | Quick web search in browser |

---

## 🛠️ Architecture

| Component | File | Technology | Description |
|---|---|---|---|
| **UI & Render** | `Menu.qml` | QML / QtQuick / LayerShellQt | Frameless Wayland overlay, neon aesthetics, smooth animations |
| **System Bridge** | `bridge.py` | PySide6 / Python 3 | IPC socket server, app scanner, clipboard/theme providers |
| **Data Engine** | `MenuModel.js` | JavaScript (ES6) | Fuzzy matching, search scoring, JSONC tree traversal |
| **Menu Schema** | `menu.jsonc` | JSONC | Declarative menu hierarchy, actions, and icons |

---

## 📦 Installation & Setup

### Requirements
- Linux with KDE Plasma 6 (Wayland)
- Python 3 with `PySide6`
- `layer-shell-qt`
- Nerd Font (e.g., `MesloLGS Nerd Font`)

### Quick Install
```bash
# Clone the repository
git clone https://github.com/<your-username>/ravenspot.git ~/.local/share/ravenspot

# Link executable
mkdir -p ~/.local/bin
ln -sf ~/.local/share/ravenspot/bridge.py ~/.local/bin/ravenspot
chmod +x ~/.local/share/ravenspot/bridge.py

# Install desktop shortcut
cp ~/.local/share/ravenspot/ravenspot.desktop ~/.local/share/applications/
kbuildsycoca6 --noincremental
```

---

## 🔧 Customization

You can override or extend the menu without touching core files by creating:
`~/.config/omarchy-menu/menu.jsonc`

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
Copyright (c) 2026 dokuro (Raven).
