#!/usr/bin/env python3
"""
RavenSpot — Spotlight-style Launcher for KDE Plasma 6 / CachyOS.

Architecture:
  - menu.jsonc    : declarative menu definition
  - MenuModel.js  : search, hierarchy, and guard logic
  - Menu.qml      : floating window UI (Wayland/X11)
  - bridge.py     : daemon and system integration bridge (Python/Qt6)
"""

import json
import os
import re
import shutil
import socket
import subprocess
import sys

# ── Platform Path & IPC Definitions ───────────────────────────
IS_WINDOWS = sys.platform == "win32"

if IS_WINDOWS:
    USER_CONFIG_DIR = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "ravenspot")
    SERVER_NAME = "ravenspot_ipc_socket"
else:
    USER_CONFIG_DIR = os.path.expanduser("~/.config/ravenspot")
    SERVER_NAME = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ravenspot.sock")

SOCKET_PATH = SERVER_NAME

# PyInstaller bundle path resolution (frozen binary)
if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
    APP_DIR = sys._MEIPASS
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))

LEGACY_CONFIG_DIR = USER_CONFIG_DIR
MENU_PATH = os.path.join(APP_DIR, "menu.jsonc")
USER_MENU_PATH = os.path.join(USER_CONFIG_DIR, "menu.jsonc")

def _try_fast_client():
    if "unittest" in sys.modules or os.environ.get("RAVENSPOT_TESTING") == "1":
        return True

    cmd = "toggle"
    start_hidden = False
    for arg in sys.argv[1:]:
        clean = arg.lstrip("-").lower()
        if clean in ("toggle", "show", "hide", "quit", "reload"):
            cmd = clean
        elif clean in ("daemon", "start-daemon"):
            start_hidden = True
            cmd = None

    if cmd:
        try:
            if hasattr(socket, "AF_UNIX") and not IS_WINDOWS:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(0.2)
                s.connect(SOCKET_PATH)
                s.sendall((cmd + "\n").encode("utf-8"))
                s.close()
                sys.exit(0)
            else:
                from PySide6.QtNetwork import QLocalSocket
                s = QLocalSocket()
                s.connectToServer(SERVER_NAME)
                if s.waitForConnected(300):
                    s.write((cmd + "\n").encode("utf-8"))
                    s.waitForBytesWritten(300)
                    s.disconnectFromServer()
                    sys.exit(0)
        except Exception:
            if cmd == "quit":
                sys.exit(0)
    return start_hidden

START_HIDDEN = _try_fast_client()

from PySide6.QtCore import QObject, QUrl, Qt, Slot, QTimer, QSize, Signal, Property, QThread
from PySide6.QtMultimedia import QSoundEffect
from PySide6.QtGui import QGuiApplication, QIcon, QImage
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickImageProvider
from PySide6.QtNetwork import QLocalServer, QLocalSocket

LEGACY_CONFIG_DIR = os.path.join(os.path.expanduser("~"), ".config", "omarchy-menu")
USER_MENU_PATH = os.path.join(USER_CONFIG_DIR, "menu.jsonc")
if not os.path.exists(USER_MENU_PATH) and os.path.exists(os.path.join(LEGACY_CONFIG_DIR, "menu.jsonc")):
    USER_MENU_PATH = os.path.join(LEGACY_CONFIG_DIR, "menu.jsonc")

class Win32HotkeyThread(QThread):
    """Windows global hotkey listener thread (Alt+Space and Win+Space)."""
    hotkeyTriggered = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._running = False
        self._thread_id = None

    def run(self):
        if not IS_WINDOWS:
            return
        import ctypes
        from ctypes import wintypes

        user32 = getattr(ctypes, "windll", None) and getattr(ctypes.windll, "user32", None)
        kernel32 = getattr(ctypes, "windll", None) and getattr(ctypes.windll, "kernel32", None)
        if not user32 or not kernel32:
            return

        MOD_ALT = 0x0001
        MOD_WIN = 0x0008
        MOD_NOREPEAT = 0x4000
        VK_SPACE = 0x20
        WM_HOTKEY = 0x0312

        self._running = True
        try:
            self._thread_id = kernel32.GetCurrentThreadId()
        except Exception:
            self._thread_id = None

        # id 1: Alt + Space, id 2: Win + Space
        try:
            user32.RegisterHotKey(None, 1, MOD_ALT | MOD_NOREPEAT, VK_SPACE)
            user32.RegisterHotKey(None, 2, MOD_WIN | MOD_NOREPEAT, VK_SPACE)
        except Exception:
            pass

        try:
            msg = wintypes.MSG()
            while self._running:
                ret = user32.GetMessageW(ctypes.byref(msg), None, 0, 0)
                if ret <= 0:
                    break
                if msg.message == WM_HOTKEY:
                    self.hotkeyTriggered.emit()
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
        except Exception:
            pass
        finally:
            try:
                user32.UnregisterHotKey(None, 1)
                user32.UnregisterHotKey(None, 2)
            except Exception:
                pass

    def stop(self):
        self._running = False
        if IS_WINDOWS and self._thread_id:
            try:
                import ctypes
                WM_QUIT = 0x0012
                if getattr(ctypes, "windll", None) and getattr(ctypes.windll, "user32", None):
                    ctypes.windll.user32.PostThreadMessageW(self._thread_id, WM_QUIT, 0, 0)
            except Exception:
                pass
        self.wait(1000)

# XDG application dirs (uygulama provider'ı için)
def scan_windows_directory(base_dir):
    """Windows Başlat Menüsü (.lnk / .url) kısayollarını tara."""
    apps = []
    if not os.path.isdir(base_dir):
        return apps

    ignore_keywords = {"uninstall", "kaldır", "setup", "help", "readme", "documentation"}
    for root, dirs, files in os.walk(base_dir):
        for f in sorted(files):
            ext = os.path.splitext(f)[1].lower()
            if ext in (".lnk", ".url"):
                label = os.path.splitext(f)[0]
                if any(ign in label.lower() for ign in ignore_keywords):
                    continue
                full_path = os.path.join(root, f)
                slug = re.sub(r"[^\w\s-]", "", label.lower()).strip().replace(" ", "-")
                aliases = [w.lower() for w in re.split(r"[\s_-]+", label) if len(w) > 1]

                cat = "utilities"
                lower_path = full_path.lower()
                if any(k in lower_path for k in ["game", "steam", "riot", "epic", "oyun"]):
                    cat = "games"
                elif any(k in lower_path for k in ["browser", "chrome", "edge", "firefox", "brave", "zen", "internet"]):
                    cat = "internet"
                elif any(k in lower_path for k in ["code", "studio", "dev", "git", "python", "terminal"]):
                    cat = "development"
                elif any(k in lower_path for k in ["music", "video", "media", "spotify", "vlc", "audio"]):
                    cat = "multimedia"
                elif any(k in lower_path for k in ["photo", "paint", "gimp", "photoshop", "figma"]):
                    cat = "graphics"
                elif any(k in lower_path for k in ["word", "excel", "powerpoint", "office", "pdf"]):
                    cat = "office"
                elif any(k in lower_path for k in ["settings", "system", "ayar"]):
                    cat = "system"

                apps.append({
                    "id": slug or label,
                    "label": label,
                    "icon": full_path,
                    "aliases": aliases,
                    "desktop": full_path,
                    "category": cat
                })
    return apps

def _xdg_data_dirs():
    dirs = []
    home = os.path.expanduser("~")
    dirs.append(os.path.join(home, ".local", "share", "applications"))
    dirs.append(os.path.join(home, ".local", "share", "flatpak", "exports", "share", "applications"))
    dirs.append("/var/lib/flatpak/exports/share/applications")
    for d in os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":"):
        dirs.append(os.path.join(d, "applications"))
    return dirs


class Shell(QObject):
    """QML'e Python köprüsü. Tüm yan etkiler (kabuk, guard, provider, app) burada."""

    reopenRequested = Signal()
    toggleRequested = Signal()
    showRequested = Signal()
    hideRequested = Signal()
    reloadRequested = Signal()

    def __init__(self, start_hidden=False):
        super().__init__()
        self._start_hidden = start_hidden
        self._init_extra()
        self._menu_path = MENU_PATH
        self._user_menu_path = USER_MENU_PATH
        self._apps_cache = None  # [ {id,label,icon,aliases,keywords,desktop} ]

    @Property(bool, constant=True)
    def startHidden(self):
        return self._start_hidden

    @Slot(bool)
    def onMenuVisibilityChanged(self, visible):
        if visible:
            self._update_telemetry()
            if hasattr(self, "_telemetry_timer") and not self._telemetry_timer.isActive():
                self._telemetry_timer.start(1000)
        else:
            if hasattr(self, "_telemetry_timer") and self._telemetry_timer.isActive():
                self._telemetry_timer.stop()

    # ── Dosya okuma ──────────────────────────────────────────────
    @Slot(str, result=str)
    def readFile(self, path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    @Slot(result=str)
    def menuPath(self):
        return self._menu_path

    @Slot(result=str)
    def userMenuPath(self):
        return self._user_menu_path

    # ── Kabuk (guard batch + provider enumeration) ───────────────
    @Slot(str, result=str)
    def bash(self, script):
        """Senkron bash çalıştır, stdout döndür (guard batch'leri için)."""
        if not script or not script.strip():
            return ""
        try:
            p = subprocess.run(
                ["bash", "-c", script],
                capture_output=True, text=True, timeout=20,
            )
            return p.stdout or ""
        except Exception as e:
            return ""

    @Slot(str)
    def runDetached(self, command):
        """Eylemi arka planda, menü kapanınca ölmeden çalıştır."""
        if not command or not command.strip():
            return
        subprocess.Popen(
            ["bash", "-c", command],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,  # menü kapanınca SIGHUP almasın
        )

    # ── Provider: apps ───────────────────────────────────────────
    def _categorize_app(self, categories_str):
        c = categories_str or ""
        if "Game" in c:
            return "games"
        if any(x in c for x in ["Network", "WebBrowser", "Email", "Chat", "Feed", "FileTransfer"]):
            return "internet"
        if any(x in c for x in ["Development", "IDE", "Debugger", "Building"]):
            return "development"
        if any(x in c for x in ["AudioVideo", "Audio", "Video", "Player", "Recorder", "Music"]):
            return "multimedia"
        if any(x in c for x in ["Graphics", "Photography", "RasterGraphics", "VectorGraphics"]):
            return "graphics"
        if any(x in c for x in ["Office", "Viewer", "WordProcessor", "Spreadsheet"]):
            return "office"
        if any(x in c for x in ["System", "Settings", "Security", "Monitor"]):
            return "system"
        return "utilities"

    def _load_apps(self):
        if self._apps_cache is not None:
            return self._apps_cache

        if IS_WINDOWS:
            apps = []
            seen = set()
            start_dirs = [
                os.path.expandvars(r"%ProgramData%\Microsoft\Windows\Start Menu\Programs"),
                os.path.expandvars(r"%APPDATA%\Microsoft\Windows\Start Menu\Programs"),
            ]
            for d in start_dirs:
                for app in scan_windows_directory(d):
                    k = app["label"].lower()
                    if k not in seen:
                        seen.add(k)
                        apps.append(app)
            self._apps_cache = apps
            return apps

        import configparser
        seen = {}
        for d in _xdg_data_dirs():
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                if not name.endswith(".desktop"):
                    continue
                full = os.path.join(d, name)
                if name in seen:
                    continue
                seen[name] = full

        apps = []
        for name, full in seen.items():
            cp = configparser.ConfigParser(interpolation=None)
            try:
                cp.read(full, encoding="utf-8")
            except Exception:
                continue
            if not cp.has_section("Desktop Entry"):
                continue
            de = cp["Desktop Entry"]
            if de.get("Type", "Application") != "Application":
                continue
            if de.get("NoDisplay", "false").lower() == "true":
                continue
            if de.get("Hidden", "false").lower() == "true":
                continue
            label = de.get("Name", name[:-8])
            if not label:
                continue
            aliases = []
            gn = de.get("GenericName", "")
            if gn:
                aliases.append(gn)
            kw = de.get("Keywords", "")
            if kw:
                aliases.extend(k.strip() for k in kw.replace(";", " ").split() if k.strip())
            cat = self._categorize_app(de.get("Categories", ""))
            apps.append({
                "id": name[:-8],
                "label": label,
                "icon": de.get("Icon", ""),
                "aliases": aliases,
                "desktop": full,
                "category": cat,
            })

        apps.sort(key=lambda a: a["label"].lower())
        self._apps_cache = apps
        return apps

    @Slot(result=str)
    def listApps(self):
        return json.dumps(self._load_apps())

    @Slot(str, result=str)
    def listAppsCategory(self, cat):
        all_apps = self._load_apps()
        if not cat or cat == "all":
            return json.dumps(all_apps)
        return json.dumps([a for a in all_apps if a.get("category") == cat])

    @Slot(result=str)
    def getAppCategoryCounts(self):
        all_apps = self._load_apps()
        counts = {
            "games": 0, "internet": 0, "development": 0,
            "multimedia": 0, "graphics": 0, "office": 0,
            "system": 0, "utilities": 0, "all": len(all_apps)
        }
        for a in all_apps:
            cat = a.get("category", "utilities")
            if cat in counts:
                counts[cat] += 1
            else:
                counts["utilities"] += 1
        return json.dumps(counts)

    @Slot(str)
    def launchApp(self, desktop):
        """Bir uygulamayı / kısayolu başlat."""
        if not desktop:
            return
        if IS_WINDOWS:
            try:
                os.startfile(desktop)
            except Exception:
                subprocess.Popen(["cmd", "/c", "start", "", desktop], shell=True)
            return

        subprocess.Popen(
            ["kioclient", "exec", desktop],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: colorschemes & favorites ───────────────────────
    FAV_THEMES_PATH = os.path.join(USER_CONFIG_DIR, "favorite-themes.json")

    def _load_favorite_themes(self):
        try:
            target = self.FAV_THEMES_PATH
            if not os.path.isfile(target) and os.path.isfile(os.path.join(LEGACY_CONFIG_DIR, "favorite-themes.json")):
                target = os.path.join(LEGACY_CONFIG_DIR, "favorite-themes.json")
            if os.path.isfile(target):
                with open(target, "r", encoding="utf-8") as f:
                    return json.load(f)
        except Exception:
            pass
        return []

    def _save_favorite_themes(self, favs):
        try:
            os.makedirs(os.path.dirname(self.FAV_THEMES_PATH), exist_ok=True)
            with open(self.FAV_THEMES_PATH, "w", encoding="utf-8") as f:
                json.dump(favs, f, indent=2)
        except Exception:
            pass

    @Slot(str, result=bool)
    def toggleFavoriteTheme(self, name):
        if not name:
            return False
        favs = self._load_favorite_themes()
        if name in favs:
            favs.remove(name)
            is_fav = False
        else:
            favs.append(name)
            is_fav = True
        self._save_favorite_themes(favs)
        return is_fav

    @Slot(result=str)
    def listColorschemes(self):
        current = ""
        try:
            current = subprocess.run(
                ["kreadconfig6", "--file", "kdeglobals", "--group", "General", "--key", "ColorScheme"],
                capture_output=True, text=True, timeout=3,
            ).stdout.strip()
        except Exception:
            pass

        names = []
        dirs = [
            os.path.expanduser("~/.local/share/color-schemes"),
            "/usr/share/color-schemes",
        ]
        for d in dirs:
            if os.path.isdir(d):
                for f in sorted(os.listdir(d)):
                    if f.endswith(".colors"):
                        n = f[:-7]
                        if n not in names:
                            names.append(n)

        favs = set(self._load_favorite_themes())

        rows = []
        # 1. Şu An Kullanılan Tema (Current Theme)
        if current and current in names:
            rows.append({
                "value": current,
                "label": current,
                "current": True,
                "isFavorite": current in favs,
                "section": "current",
            })

        # 2. Favoriler (Favorites)
        for n in sorted(names):
            if n == current:
                continue
            if n in favs:
                rows.append({
                    "value": n,
                    "label": n,
                    "current": False,
                    "isFavorite": True,
                    "section": "favorite",
                })

        # 3. Diğer Temalar (Other Themes)
        for n in sorted(names):
            if n == current or n in favs:
                continue
            rows.append({
                "value": n,
                "label": n,
                "current": False,
                "isFavorite": False,
                "section": "other",
            })

        return json.dumps(rows)

    @Slot(str)
    def applyColorscheme(self, name):
        if not name:
            return
        subprocess.Popen(
            ["plasma-apply-colorscheme", name],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: power-profiles ─────────────────────────────────
    @Slot(result=str)
    def listPowerProfiles(self):
        try:
            p = subprocess.run(
                ["powerprofilesctl", "list"],
                capture_output=True, text=True, timeout=5,
            )
        except Exception:
            return "[]"
        current = ""
        try:
            current = subprocess.run(
                ["powerprofilesctl", "get"],
                capture_output=True, text=True, timeout=5,
            ).stdout.strip()
        except Exception:
            pass

        profiles = []
        for line in p.stdout.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            # Profil adları "performance:" / "* balanced:" biçiminde (2 boşluk girintili),
            # alt alanlar (CpuDriver:, Degraded: vb.) 4+ boşluk girintilidir.
            indent = len(line) - len(line.lstrip())
            if indent >= 4:
                continue
            if stripped.startswith("*"):
                stripped = stripped[1:].strip()
            if stripped.endswith(":"):
                name = stripped.rstrip(":").strip()
                if name and name not in profiles:
                    profiles.append(name)

        labels = {
            "performance": "Performance",
            "balanced": "Balanced",
            "power-saver": "Power Saver",
        }
        seen = []
        rows = []
        for name in profiles:
            if name in seen:
                continue
            seen.append(name)
            rows.append({
                "value": name,
                "label": labels.get(name, name),
                "current": name == current,
            })
        return json.dumps(rows)

    @Slot(str)
    def applyPowerProfile(self, profile):
        if not profile:
            return
        subprocess.Popen(
            ["powerprofilesctl", "set", profile],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: clipboard ───────────────────────────────────────
    @Slot(result=str)
    def listClipboard(self):
        try:
            p = subprocess.run(
                ["qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.getClipboardHistoryMenu"],
                capture_output=True, text=True, timeout=2,
            )
            rows = []
            seen = set()
            for line in p.stdout.splitlines():
                stripped = line.strip()
                if not stripped or stripped in seen:
                    continue
                seen.add(stripped)
                display = stripped.replace("\n", " ").replace("\t", " ")
                if len(display) > 60:
                    display = display[:57] + "..."
                rows.append({
                    "label": display,
                    "value": stripped,
                })
                if len(rows) >= 30:
                    break
            return json.dumps(rows)
        except Exception:
            return "[]"

    @Slot(str)
    def setClipboard(self, text):
        if not text:
            return
        try:
            subprocess.run(
                ["qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.setClipboardContents", text],
                timeout=1,
            )
        except Exception:
            pass

    # ── Provider: files (downloads / user dirs) ───────────────────
    @Slot(str, result=str)
    def listFolderFiles(self, folder_name="downloads"):
        import datetime, mimetypes
        home = os.path.expanduser("~")
        mapping = {
            "downloads": ["İndirilenler", "Downloads"],
            "desktop": ["Masaüstü", "Desktop"],
            "documents": ["Belgeler", "Documents"],
            "pictures": ["Resimler", "Pictures"],
            "videos": ["Videolar", "Videos"],
            "music": ["Müzik", "Music"],
        }
        target_dir = None
        for candidate in mapping.get(folder_name, [folder_name]):
            p = os.path.join(home, candidate)
            if os.path.isdir(p):
                target_dir = p
                break
        if not target_dir:
            return "[]"

        def _fmt_size(sz):
            for unit in ["B", "KB", "MB", "GB", "TB"]:
                if sz < 1024.0:
                    return f"{sz:.1f} {unit}" if unit != "B" else f"{sz} B"
                sz /= 1024.0
            return f"{sz:.1f} PB"

        def _icon_for(name, is_dir):
            if is_dir:
                return "folder"
            ext = os.path.splitext(name)[1].lower()
            ext_map = {
                ".png": "image-png", ".jpg": "image-jpeg", ".jpeg": "image-jpeg", ".webp": "image-x-generic",
                ".gif": "image-gif", ".svg": "image-x-generic",
                ".tar": "application-x-tar", ".gz": "application-x-tar", ".zip": "application-zip",
                ".xz": "application-x-tar", ".7z": "application-x-7z-compressed",
                ".pdf": "application-pdf", ".txt": "text-plain", ".md": "text-markdown",
                ".mp4": "video-mp4", ".mkv": "video-x-generic", ".mp3": "audio-mp3",
                ".sh": "application-x-shellscript", ".py": "text-x-python",
                ".json": "application-json", ".desktop": "application-x-desktop"
            }
            if ext in ext_map:
                return ext_map[ext]
            mime, _ = mimetypes.guess_type(name)
            if mime:
                return mime.replace("/", "-")
            return "application-octet-stream"

        rows = []
        try:
            for entry in os.scandir(target_dir):
                try:
                    st = entry.stat()
                    is_d = entry.is_dir()
                    dt = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%d.%m %H:%M")
                    sz_str = "Klasör" if is_d else _fmt_size(st.st_size)
                    icon_nm = _icon_for(entry.name, is_d)
                    rows.append({
                        "name": entry.name,
                        "path": entry.path,
                        "is_dir": is_d,
                        "mtime": st.st_mtime,
                        "desc": f"{sz_str} • {dt}",
                        "iconName": icon_nm
                    })
                except Exception:
                    continue
        except Exception:
            return "[]"

        rows.sort(key=lambda x: x["mtime"], reverse=True)
        return json.dumps(rows[:80])

    @Slot(str)
    def openPath(self, path):
        if not path:
            return
        try:
            expanded = os.path.expandvars(os.path.expanduser(path))
            if IS_WINDOWS:
                if os.path.isdir(expanded):
                    subprocess.Popen(["explorer.exe", os.path.normpath(expanded)])
                else:
                    if hasattr(os, "startfile"):
                        os.startfile(expanded)
                    else:
                        subprocess.Popen(["explorer.exe", os.path.normpath(expanded)])
                return
            if os.path.isdir(expanded):
                if shutil.which("dolphin"):
                    subprocess.Popen(["dolphin", os.path.abspath(expanded)])
                    return
            subprocess.Popen(["xdg-open", expanded])
        except Exception:
            pass

    @Slot(str, result=str)
    def getAppActions(self, desktop):
        """Bir .desktop dosyasındaki Action tanımlarını (yeni pencere vb.) döndür."""
        if not desktop:
            return "[]"
        expanded = os.path.expanduser(desktop)
        if not os.path.isfile(expanded):
            return "[]"
        import configparser
        cp = configparser.ConfigParser(interpolation=None)
        try:
            cp.read(expanded, encoding="utf-8")
        except Exception:
            return "[]"
        actions = []
        if cp.has_section("Desktop Entry"):
            de = cp["Desktop Entry"]
            raw_actions = de.get("Actions", "")
            action_names = [a.strip() for a in raw_actions.split(";") if a.strip()]
            for act in action_names:
                sec = f"Desktop Action {act}"
                if cp.has_section(sec):
                    act_sec = cp[sec]
                    name = act_sec.get("Name", act)
                    raw_exec = act_sec.get("Exec", "")
                    if name and raw_exec:
                        clean_exec = re.sub(r"%[a-zA-Z]", "", raw_exec).strip()
                        actions.append({"id": act, "name": name, "exec": clean_exec})
        return json.dumps(actions)

    @Slot(str)
    def openContainingFolder(self, path):
        """Dosyanın veya uygulamanın bulunduğu klasörü dosya yöneticisi ile açıp dosyayı seç."""
        if not path:
            return
        try:
            expanded = os.path.expandvars(os.path.expanduser(path))
            if not os.path.exists(expanded):
                return
            if IS_WINDOWS:
                norm = os.path.normpath(expanded)
                if os.path.isdir(norm):
                    subprocess.Popen(["explorer.exe", norm])
                else:
                    subprocess.Popen(["explorer.exe", f"/select,{norm}"])
                return
            if shutil.which("dolphin"):
                subprocess.Popen(["dolphin", "--select", os.path.abspath(expanded)])
            else:
                target = expanded if os.path.isdir(expanded) else os.path.dirname(expanded)
                subprocess.Popen(["xdg-open", target])
        except Exception:
            pass

    @Slot(str)
    def openInTerminal(self, path):
        """Belirtilen dosya veya klasörün konumunda terminal aç."""
        if not path:
            return
        try:
            expanded = os.path.expandvars(os.path.expanduser(path))
            target_dir = expanded if os.path.isdir(expanded) else os.path.dirname(expanded)
            if IS_WINDOWS:
                norm_dir = os.path.normpath(target_dir)
                if shutil.which("wt.exe") or shutil.which("wt"):
                    subprocess.Popen(["wt.exe", "-d", norm_dir])
                elif shutil.which("powershell.exe") or shutil.which("powershell"):
                    subprocess.Popen(["powershell.exe", "-NoExit", "-Command", f"Set-Location -LiteralPath '{norm_dir}'"])
                else:
                    subprocess.Popen(["cmd.exe", "/K", f"cd /d \"{norm_dir}\""])
                return
            if shutil.which("konsole"):
                subprocess.Popen(["konsole", "--workdir", target_dir])
            else:
                subprocess.Popen(["x-terminal-emulator"], cwd=target_dir)
        except Exception:
            pass

    @Slot(str)
    def runInTerminal(self, desktop_or_cmd):
        """Uygulamayı veya komutu terminal içinde çalıştır."""
        if not desktop_or_cmd:
            return
        try:
            cmd = desktop_or_cmd
            if IS_WINDOWS:
                if shutil.which("wt.exe") or shutil.which("wt"):
                    subprocess.Popen(["wt.exe", "powershell.exe", "-NoExit", "-Command", cmd])
                elif shutil.which("powershell.exe") or shutil.which("powershell"):
                    subprocess.Popen(["powershell.exe", "-NoExit", "-Command", cmd])
                else:
                    subprocess.Popen(["cmd.exe", "/K", cmd])
                return

            if desktop_or_cmd.endswith(".desktop") and os.path.isfile(desktop_or_cmd):
                import configparser
                cp = configparser.ConfigParser(interpolation=None)
                cp.read(desktop_or_cmd, encoding="utf-8")
                if cp.has_section("Desktop Entry"):
                    cmd = cp["Desktop Entry"].get("Exec", desktop_or_cmd)
                    cmd = re.sub(r"%[a-zA-Z]", "", cmd).strip()
            if shutil.which("konsole"):
                subprocess.Popen(["konsole", "-e", "bash", "-c", f"{cmd}; echo; read -p 'Kapatmak için Enter basınız...'"])
            else:
                subprocess.Popen(["x-terminal-emulator", "-e", cmd])
        except Exception:
            pass

    @Slot(str)
    def openInDiscover(self, desktop):
        """Uygulamayı uygulama mağazasında veya klasöründe aç."""
        if not desktop:
            return
        try:
            if IS_WINDOWS:
                self.openContainingFolder(desktop)
                return
            if shutil.which("plasma-discover"):
                subprocess.Popen(["plasma-discover", "--open-desktop-file", desktop])
            else:
                self.openContainingFolder(desktop)
        except Exception:
            pass

    @Slot(str)
    def editDesktopFile(self, desktop):
        """Uygulama kısayolunu veya yapılandırmasını düzenleyicide aç."""
        if not desktop:
            return
        try:
            if IS_WINDOWS:
                norm = os.path.normpath(desktop)
                if shutil.which("notepad.exe") or shutil.which("notepad"):
                    subprocess.Popen(["notepad.exe", norm])
                else:
                    self.openContainingFolder(desktop)
                return
            if shutil.which("kmenuedit"):
                subprocess.Popen(["kmenuedit"])
            elif shutil.which("kate"):
                subprocess.Popen(["kate", desktop])
            else:
                subprocess.Popen(["xdg-open", desktop])
        except Exception:
            pass

    @Slot(str, result=bool)
    def moveToTrash(self, path):
        """Dosyayı veya klasörü çöp kutusuna taşı."""
        if not path:
            return False
        try:
            expanded = os.path.expandvars(os.path.expanduser(path))
            if not os.path.exists(expanded):
                return False
            if IS_WINDOWS:
                try:
                    from ctypes import wintypes
                    class SHFILEOPSTRUCTW(ctypes.Structure):
                        _fields_ = [
                            ("hwnd", wintypes.HWND),
                            ("wFunc", wintypes.UINT),
                            ("pFrom", wintypes.LPCWSTR),
                            ("pTo", wintypes.LPCWSTR),
                            ("fFlags", wintypes.WORD),
                            ("fAnyOperationsAborted", wintypes.BOOL),
                            ("hNameMappings", wintypes.LPVOID),
                            ("lpszProgressTitle", wintypes.LPCWSTR),
                        ]
                    FO_DELETE = 0x0003
                    FOF_ALLOWUNDO = 0x0040
                    FOF_NOCONFIRMATION = 0x0010
                    FOF_SILENT = 0x0004
                    
                    norm = os.path.normpath(expanded) + '\0'
                    fileop = SHFILEOPSTRUCTW()
                    fileop.wFunc = FO_DELETE
                    fileop.pFrom = norm
                    fileop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT
                    ret = ctypes.windll.shell32.SHFileOperationW(ctypes.byref(fileop))
                    return ret == 0 and not fileop.fAnyOperationsAborted
                except Exception:
                    return False
            if shutil.which("gio"):
                res = subprocess.run(["gio", "trash", expanded], capture_output=True, timeout=5)
                return res.returncode == 0
            elif shutil.which("kioclient"):
                res = subprocess.run(["kioclient", "move", expanded, "trash:/"], capture_output=True, timeout=5)
                return res.returncode == 0
        except Exception:
            pass
        return False



    # ── Raven Themes, Telemetry & Sound Effects ──────────────────
    themeChanged = Signal()
    statsUpdated = Signal(int, int, int, bool)

    def _init_extra(self):
        self._themes_file = os.path.join(APP_DIR, "themes.json")
        self._user_theme_file = os.path.join(USER_CONFIG_DIR, "theme.json")
        os.makedirs(os.path.dirname(self._user_theme_file), exist_ok=True)
        
        # Load themes
        with open(self._themes_file) as f:
            self._themes_data = json.load(f)
        
        # Read saved user choice
        if os.path.exists(self._user_theme_file):
            try:
                with open(self._user_theme_file) as f:
                    saved = json.load(f)
                    if saved.get("active") in self._themes_data["themes"]:
                        self._themes_data["active"] = saved["active"]
            except Exception:
                pass

        # Apply current theme icon theme to QIcon
        cur_icon_theme = "candy-icons"
        QIcon.setThemeName(cur_icon_theme)

        # Sounds
        self._snd_thock = QSoundEffect()
        self._snd_thock.setSource(QUrl.fromLocalFile(os.path.join(APP_DIR, "sounds", "thock.wav")))
        self._snd_thock.setVolume(0.08)

        self._snd_creamy = QSoundEffect()
        self._snd_creamy.setSource(QUrl.fromLocalFile(os.path.join(APP_DIR, "sounds", "creamy.wav")))
        self._snd_creamy.setVolume(0.10)

        # Telemetry timer
        self._last_cpu_idle = 0
        self._last_cpu_total = 0
        self._telemetry_timer = QTimer(self)
        self._telemetry_timer.setInterval(1000)
        self._telemetry_timer.timeout.connect(self._update_telemetry)
        if not getattr(self, "_start_hidden", False):
            self._telemetry_timer.start()

    def _update_telemetry(self):
        # CPU
        cpu_pct = 0
        if IS_WINDOWS:
            try:
                import ctypes
                from ctypes import wintypes
                idle_t, kern_t, user_t = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
                if ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle_t), ctypes.byref(kern_t), ctypes.byref(user_t)):
                    def _to_i(ft): return (ft.dwHighDateTime << 32) + ft.dwLowDateTime
                    idle = _to_i(idle_t)
                    total = _to_i(kern_t) + _to_i(user_t)
                    if self._last_cpu_total > 0:
                        dt = total - self._last_cpu_total
                        di = idle - self._last_cpu_idle
                        if dt > 0:
                            cpu_pct = max(0, min(100, int(100.0 * (1.0 - di / dt))))
                    self._last_cpu_idle = idle
                    self._last_cpu_total = total
            except Exception:
                pass
        else:
            try:
                with open("/proc/stat") as f:
                    fields = [float(x) for x in f.readline().strip().split()[1:8]]
                idle = fields[3] + fields[4]
                total = sum(fields)
                if self._last_cpu_total > 0:
                    dt = total - self._last_cpu_total
                    di = idle - self._last_cpu_idle
                    if dt > 0:
                        cpu_pct = max(0, min(100, int(100.0 * (1.0 - di / dt))))
                self._last_cpu_idle = idle
                self._last_cpu_total = total
            except Exception:
                pass

        # GPU
        gpu_pct = 0
        if not IS_WINDOWS:
            try:
                for p in ["/sys/class/drm/card1/device/gpu_busy_percent", "/sys/class/drm/card0/device/gpu_busy_percent"]:
                    if os.path.exists(p):
                        with open(p) as f:
                            gpu_pct = int(f.read().strip())
                            break
            except Exception:
                pass

        # Audio volume & mute
        vol_pct = 50
        is_muted = False
        if not IS_WINDOWS:
            try:
                out = subprocess.check_output(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], timeout=1).decode().strip()
                is_muted = "[MUTED]" in out
                for part in out.split():
                    try:
                        vol_pct = int(float(part) * 100)
                        break
                    except ValueError:
                        pass
            except Exception:
                pass

        self.statsUpdated.emit(cpu_pct, gpu_pct, vol_pct, is_muted)

    @Slot(result="QVariantMap")
    def getSysMonitorInfo(self):
        tot_ram, used_ram, ram_pct = 0, 0, 0
        up_text = ""
        if IS_WINDOWS:
            try:
                import ctypes
                class MEMORYSTATUSEX(ctypes.Structure):
                    _fields_ = [
                        ("dwLength", ctypes.c_ulong),
                        ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                    ]
                stat = MEMORYSTATUSEX()
                stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
                if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat)):
                    tot_ram = stat.ullTotalPhys / (1024**3)
                    avail = stat.ullAvailPhys / (1024**3)
                    used_ram = tot_ram - avail
                    ram_pct = int(stat.dwMemoryLoad)

                up_sec = int(ctypes.windll.kernel32.GetTickCount64() / 1000)
                up_hrs = up_sec // 3600
                up_mins = (up_sec % 3600) // 60
                up_text = f"{up_hrs}s {up_mins}d"
            except Exception:
                pass
        else:
            try:
                with open("/proc/meminfo") as f:
                    mem = {}
                    for line in f:
                        parts = line.split(":")
                        if len(parts) == 2:
                            mem[parts[0].strip()] = int(parts[1].strip().split()[0])
                tot_ram = mem.get("MemTotal", 0) / 1024 / 1024
                avail_ram = mem.get("MemAvailable", 0) / 1024 / 1024
                used_ram = tot_ram - avail_ram
                ram_pct = int((used_ram / tot_ram) * 100) if tot_ram > 0 else 0
            except Exception:
                pass

            try:
                with open("/proc/uptime") as f:
                    up_sec = int(float(f.readline().split()[0]))
                up_hrs = up_sec // 3600
                up_mins = (up_sec % 3600) // 60
                up_text = f"{up_hrs}s {up_mins}d"
            except Exception:
                pass

        # Disk (shutil.disk_usage works on both Windows and Linux)
        disk_tot, disk_used, disk_pct = 0, 0, 0
        try:
            root_drive = (os.environ.get("SystemDrive", "C:") + "\\") if IS_WINDOWS else "/"
            total_b, used_b, free_b = shutil.disk_usage(root_drive)
            disk_tot = total_b / (1024**3)
            disk_used = used_b / (1024**3)
            disk_pct = int((disk_used / disk_tot) * 100) if disk_tot > 0 else 0
        except Exception:
            pass

        # CPU Temp
        temp_c = 0
        if not IS_WINDOWS:
            try:
                for t in ["/sys/class/hwmon/hwmon4/temp6_input", "/sys/class/hwmon/hwmon3/temp1_input", "/sys/class/thermal/thermal_zone0/temp"]:
                    if os.path.exists(t):
                        with open(t) as f:
                            val = int(f.read().strip())
                            if val > 1000: val = int(val / 1000)
                            if 20 <= val <= 105:
                                temp_c = val
                                break
            except Exception:
                pass

        return {
            "ram_text": f"{used_ram:.1f}/{tot_ram:.0f}G",
            "ram_pct": ram_pct,
            "disk_text": f"{disk_used:.0f}/{disk_tot:.0f}G",
            "disk_pct": disk_pct,
            "temp": f"{temp_c}°C" if temp_c else "--",
            "uptime": up_text or "--"
        }

    @Slot(result=str)
    def getActiveOutputName(self):
        try:
            out = subprocess.check_output(
                ["busctl", "--user", "call", "org.kde.KWin", "/KWin", "org.kde.KWin", "activeOutputName"],
                timeout=0.5
            ).decode().strip()
            if '"' in out:
                return out.split('"')[1]
        except Exception:
            pass
        return ""

    @Slot(QObject)
    def centerWindowOnActiveScreen(self, qquick_window):
        target_screen = None
        if not IS_WINDOWS:
            out_name = self.getActiveOutputName()
            for s in QGuiApplication.screens():
                if s.name() == out_name:
                    target_screen = s
                    break
        if not target_screen:
            from PySide6.QtGui import QCursor
            target_screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
        if target_screen and qquick_window:
            geom = target_screen.geometry()
            qquick_window.setScreen(target_screen)
            w = qquick_window.width() if qquick_window.width() > 0 else 728
            h = qquick_window.height() if qquick_window.height() > 0 else 548
            x = geom.x() + (geom.width() - w) // 2
            y = geom.y() + (geom.height() - h) // 2
            qquick_window.setPosition(x, y)

    @Slot(result="QVariantMap")
    def getInitialAudioInfo(self):
        vol_pct = 50
        is_muted = False
        try:
            out = subprocess.check_output(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], timeout=1).decode().strip()
            is_muted = "[MUTED]" in out
            for part in out.split():
                try:
                    vol_pct = int(float(part) * 100)
                    break
                except ValueError:
                    pass
        except Exception:
            pass
        return {"volume": vol_pct, "muted": is_muted}

    @Slot(int)
    def setSystemVolume(self, volume):
        vol = max(0, min(100, volume)) / 100.0
        if not IS_WINDOWS:
            try:
                subprocess.Popen(["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{vol:.2f}"])
            except Exception:
                pass

    @Slot()
    def toggleSystemMute(self):
        if not IS_WINDOWS:
            try:
                subprocess.Popen(["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"])
            except Exception:
                pass

    @Slot(result="QVariantMap")
    def currentTheme(self):
        act = self._themes_data.get("active", "raven_cyber")
        t = dict(self._themes_data["themes"].get(act, {}))
        t["key"] = act
        return t

    @Slot(result=str)
    def listRavenThemes(self):
        act = self._themes_data.get("active", "raven_cyber")
        res = []
        for k, v in self._themes_data["themes"].items():
            res.append({
                "key": k,
                "name": v["name"],
                "desc": v["desc"],
                "isCurrent": (k == act),
                "accent": v["accent"],
                "font": v["font"],
                "iconTheme": v["iconTheme"]
            })
        return json.dumps(res)

    @Slot(str)
    def applyRavenTheme(self, key):
        if key in self._themes_data["themes"]:
            self._themes_data["active"] = key
            try:
                with open(self._user_theme_file, "w") as f:
                    json.dump({"active": key}, f)
            except Exception:
                pass
            QIcon.setThemeName(self._themes_data["themes"][key].get("iconTheme", "candy-icons"))
            self.themeChanged.emit()

    @Slot()
    def playThock(self):
        try:
            if self._snd_thock.status() == QSoundEffect.Ready or self._snd_thock.status() == QSoundEffect.Loading:
                self._snd_thock.play()
        except Exception:
            pass

    @Slot()
    def playCreamy(self):
        try:
            if self._snd_creamy.status() == QSoundEffect.Ready or self._snd_creamy.status() == QSoundEffect.Loading:
                self._snd_creamy.play()
        except Exception:
            pass


class AppIconProvider(QQuickImageProvider):
    """QML'e `image://appicon/<desktop-id>` üzerinden uygulama ikonu verir."""

    def __init__(self, shell):
        super().__init__(QQuickImageProvider.Image)
        self._shell = shell

    def requestImage(self, id, size, requestedSize):
        clean_id = id.split("?")[0] if "?" in id else id
        sz = requestedSize if requestedSize.isValid() and requestedSize.width() > 0 else QSize(48, 48)

        if IS_WINDOWS or (os.path.exists(clean_id) and clean_id.lower().endswith((".lnk", ".exe", ".url"))):
            try:
                from PySide6.QtWidgets import QFileIconProvider
                from PySide6.QtCore import QFileInfo
                if not hasattr(self, "_file_icon_provider"):
                    self._file_icon_provider = QFileIconProvider()
                fi_icon = self._file_icon_provider.icon(QFileInfo(clean_id))
                if not fi_icon.isNull():
                    pm = fi_icon.pixmap(sz)
                    img = pm.toImage()
                    size.setWidth(img.width())
                    size.setHeight(img.height())
                    return img
            except Exception:
                pass

        icon_name = ""
        desktop = ""
        apps = self._shell._load_apps()
        for app in apps:
            if app["id"] == clean_id:
                icon_name = app["icon"]
                desktop = app["desktop"]
                break
        icon = QIcon.fromTheme(icon_name) if icon_name else QIcon()
        if icon.isNull() and icon_name and os.path.isabs(icon_name):
            # Bazı .desktop'larda Icon mutlak yol olabilir
            icon = QIcon(icon_name)
        if icon.isNull():
            icon = QIcon.fromTheme("application-x-executable")
        sz = requestedSize if requestedSize.isValid() and requestedSize.width() > 0 else QSize(48, 48)
        pm = icon.pixmap(sz)
        img = pm.toImage()
        # QQuickImageProvider.requestImage, QImage döndürmeyi bekler; gerçek boyut
        # referansla verilen `size` argümanı üzerinden geri bildirilir (tuple değil).
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img


class ThemeIconProvider(QQuickImageProvider):
    """QML'e `image://themeicon/<freedesktop-ikon-adi>` üzerinden temalı ikon verir."""

    def __init__(self):
        super().__init__(QQuickImageProvider.Image)

    def requestImage(self, id, size, requestedSize):
        clean_id = id.split("?")[0] if "?" in id else id
        icon = QIcon()
        if os.path.exists(clean_id) or os.path.isabs(clean_id):
            icon = QIcon(clean_id)
        if icon.isNull():
            icon = QIcon.fromTheme(clean_id)
        if icon.isNull():
            icon = QIcon.fromTheme("application-x-executable")
        # Candy icon temasındaki 48x48 ve scalable zengin renkli ikonların seçilmesi için
        # en az 64x64 çözünürlük talep ediyoruz (places/16 monokrom ikonlar yerine).
        w = max(requestedSize.width() if requestedSize.isValid() and requestedSize.width() > 0 else 48, 64)
        h = max(requestedSize.height() if requestedSize.isValid() and requestedSize.height() > 0 else 48, 64)
        pm = icon.pixmap(QSize(w, h))
        img = pm.toImage()
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img


def main():
    start_hidden = START_HIDDEN

    # ── Daemon Başlatma ──────────────────────────────────────────
    QGuiApplication.setDesktopFileName("ravenspot")
    app = QGuiApplication(sys.argv)

    # Eski soket dosyasını / pipe temizle
    if not IS_WINDOWS and os.path.exists(SOCKET_PATH):
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
    elif IS_WINDOWS:
        QLocalServer.removeServer(SERVER_NAME)

    server = QLocalServer()
    if not server.listen(SERVER_NAME):
        print(f"ravenspot: Soket dinlenemedi ({SERVER_NAME})", file=sys.stderr)

    os.chdir(APP_DIR)
    shell = Shell(start_hidden=start_hidden)

    def handle_new_connection():
        sock = server.nextPendingConnection()
        if sock:
            def process_data(raw):
                try:
                    data = raw.decode("utf-8", errors="ignore").strip()
                    for line in data.splitlines():
                        cmd_line = line.strip().lower()
                        print(f"ravenspot: IPC komutu alındı -> {cmd_line}", flush=True)
                        if cmd_line in ("toggle", "reopen"):
                            shell.toggleRequested.emit()
                        elif cmd_line == "show":
                            shell.showRequested.emit()
                        elif cmd_line == "hide":
                            shell.hideRequested.emit()
                        elif cmd_line == "reload":
                            shell.reloadRequested.emit()
                        elif cmd_line == "quit":
                            QGuiApplication.quit()
                except Exception as e:
                    print(f"ravenspot IPC hata: {e}", flush=True)

            def on_ready_read():
                process_data(bytes(sock.readAll()))

            sock.readyRead.connect(on_ready_read)
            if sock.bytesAvailable() > 0:
                on_ready_read()

    server.newConnection.connect(handle_new_connection)

    hotkey_thread = None
    if IS_WINDOWS:
        hotkey_thread = Win32HotkeyThread()
        hotkey_thread.hotkeyTriggered.connect(shell.toggleRequested.emit)
        hotkey_thread.start()
        app.aboutToQuit.connect(hotkey_thread.stop)

    engine = QQmlApplicationEngine()
    engine.addImageProvider("appicon", AppIconProvider(shell))
    engine.addImageProvider("themeicon", ThemeIconProvider())
    ctx = engine.rootContext()
    ctx.setContextProperty("Shell", shell)

    qml_path = os.path.join(APP_DIR, "Menu.qml")
    engine.load(QUrl.fromLocalFile(qml_path))

    if not engine.rootObjects():
        print("ravenspot: QML yüklenemedi", file=sys.stderr)
        server.close()
        if os.path.exists(SOCKET_PATH):
            try:
                os.unlink(SOCKET_PATH)
            except OSError:
                pass
        sys.exit(1)

    ret = app.exec()
    server.close()
    if os.path.exists(SOCKET_PATH):
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
    sys.exit(ret)


if __name__ == "__main__":
    main()
