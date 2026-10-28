#!/usr/bin/env python3
"""
omarchy-menu — Omarchy'nin Super+Space menüsünün KDE/CachyOS portu.

Mimari:
  - menu.jsonc    : menü tanımı (Omarchy ile aynı şema)
  - MenuModel.js  : Omarchy'nin saf JS mantığı (arama, ağaç, guard) — değiştirilmeden
  - Menu.qml      : LayerShellQt overlay render katmanı (Quickshell yerine)
  - bridge.py     : bu dosya — QML'in kabuk/guard/provider/app işlerini yapan Python köprüsü

Odak stratejisi: Wayland'da focus-stealing engellidir; LayerShellQt'nin
KeyboardInteractivityExclusive özelliği pencereye klavye odağı kazandırır.
"""

import json
import os
import subprocess
import sys

from PySide6.QtCore import QObject, QUrl, Qt, Slot, QTimer, QSize, Signal, Property
from PySide6.QtMultimedia import QSoundEffect
from PySide6.QtGui import QGuiApplication, QIcon, QImage
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickImageProvider
from PySide6.QtNetwork import QLocalServer, QLocalSocket

SOCKET_NAME = "ravenspot-single-instance"

APP_DIR = os.path.dirname(os.path.abspath(__file__))
MENU_PATH = os.path.join(APP_DIR, "menu.jsonc")
USER_MENU_PATH = os.path.join(os.path.expanduser("~"), ".config", "omarchy-menu", "menu.jsonc")

# XDG application dirs (uygulama provider'ı için)
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

    def __init__(self):
        super().__init__()
        self._init_extra()
        self._menu_path = MENU_PATH
        self._user_menu_path = USER_MENU_PATH
        self._apps_cache = None  # [ {id,label,icon,aliases,keywords,desktop} ]

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
        """Bir .desktop dosyasını KDE'nin kendi mekanizmasıyla başlat."""
        if not desktop:
            return
        subprocess.Popen(
            ["kioclient", "exec", desktop],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: colorschemes & favorites ───────────────────────
    FAV_THEMES_PATH = os.path.expanduser("~/.config/omarchy-menu/favorite-themes.json")

    def _load_favorite_themes(self):
        try:
            if os.path.isfile(self.FAV_THEMES_PATH):
                with open(self.FAV_THEMES_PATH, "r", encoding="utf-8") as f:
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
            subprocess.Popen(["xdg-open", path])
        except Exception:
            pass



    # ── Raven Themes, Telemetry & Sound Effects ──────────────────
    themeChanged = Signal()
    statsUpdated = Signal(int, int, int, bool)

    def _init_extra(self):
        self._themes_file = os.path.join(APP_DIR, "themes.json")
        self._user_theme_file = os.path.expanduser("~/.config/ravenspot/theme.json")
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
        self._telemetry_timer.start()

    def _update_telemetry(self):
        # CPU
        cpu_pct = 0
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
        import glob
        # RAM
        tot_ram, used_ram, ram_pct = 0, 0, 0
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

        # Disk
        disk_tot, disk_used, disk_pct = 0, 0, 0
        try:
            statvfs = os.statvfs("/")
            disk_tot = (statvfs.f_blocks * statvfs.f_frsize) / (1024**3)
            disk_avail = (statvfs.f_bavail * statvfs.f_frsize) / (1024**3)
            disk_used = disk_tot - disk_avail
            disk_pct = int((disk_used / disk_tot) * 100) if disk_tot > 0 else 0
        except Exception:
            pass

        # Uptime
        up_text = ""
        try:
            with open("/proc/uptime") as f:
                up_sec = int(float(f.readline().split()[0]))
            up_hrs = up_sec // 3600
            up_mins = (up_sec % 3600) // 60
            up_text = f"{up_hrs}s {up_mins}d"
        except Exception:
            pass

        # CPU Temp
        temp_c = 0
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
        try:
            subprocess.Popen(["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{vol:.2f}"])
        except Exception:
            pass

    @Slot()
    def toggleSystemMute(self):
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
        sz = requestedSize if requestedSize.isValid() and requestedSize.width() > 0 else QSize(48, 48)
        pm = icon.pixmap(sz)
        img = pm.toImage()
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img


def main():
    QGuiApplication.setDesktopFileName("ravenspot")
    app = QGuiApplication(sys.argv)

    # ── Single-instance kontrolü ─────────────────────────────────
    client_socket = QLocalSocket()
    client_socket.connectToServer(SOCKET_NAME)
    if client_socket.waitForConnected(200):
        # Başka bir örnek zaten çalışıyor! Sinyal gönder ve kapan.
        client_socket.write(b"reopen\n")
        client_socket.waitForBytesWritten(200)
        client_socket.disconnectFromServer()
        sys.exit(0)

    # Sunucuyu başlat
    server = QLocalServer()
    server.removeServer(SOCKET_NAME)
    server.listen(SOCKET_NAME)

    # QML'deki göreli import'ların (MenuModel.js) çözülmesi için çalışma dizini
    # uygulama dizini olmalı.
    os.chdir(APP_DIR)

    shell = Shell()

    def handle_new_connection():
        sock = server.nextPendingConnection()
        if sock:
            def on_ready_read():
                sock.readAll()
                shell.reopenRequested.emit()
            sock.readyRead.connect(on_ready_read)

    server.newConnection.connect(handle_new_connection)

    engine = QQmlApplicationEngine()
    engine.addImageProvider("appicon", AppIconProvider(shell))
    engine.addImageProvider("themeicon", ThemeIconProvider())
    ctx = engine.rootContext()
    ctx.setContextProperty("Shell", shell)

    qml_path = os.path.join(APP_DIR, "Menu.qml")
    engine.load(QUrl.fromLocalFile(qml_path))

    if not engine.rootObjects():
        print("omarchy-menu: QML yüklenemedi", file=sys.stderr)
        server.close()
        sys.exit(1)

    ret = app.exec()
    server.close()
    sys.exit(ret)


if __name__ == "__main__":
    main()
