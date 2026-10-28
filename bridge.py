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

from PySide6.QtCore import QObject, QUrl, Qt, Slot, QTimer, QSize, Signal
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


class AppIconProvider(QQuickImageProvider):
    """QML'e `image://appicon/<desktop-id>` üzerinden uygulama ikonu verir."""

    def __init__(self, shell):
        super().__init__(QQuickImageProvider.Image)
        self._shell = shell

    def requestImage(self, id, size, requestedSize):
        # id: .desktop dosya adı (uzantısız)
        icon_name = ""
        desktop = ""
        apps = self._shell._load_apps()
        for app in apps:
            if app["id"] == id:
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
        icon = QIcon()
        if os.path.exists(id) or os.path.isabs(id):
            icon = QIcon(id)
        if icon.isNull():
            icon = QIcon.fromTheme(id)
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
