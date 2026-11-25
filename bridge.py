#!/usr/bin/env python3
"""
RavenSpot — Spotlight-style Launcher for KDE Plasma 6 / CachyOS.

Architecture:
  - menu.jsonc    : declarative menu definition
  - MenuModel.js  : search, hierarchy, and guard logic
  - Menu.qml      : floating window UI (Wayland/X11)
  - bridge.py     : daemon and system integration bridge (Python/Qt6)
"""

import ctypes
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading

__version__ = "0.3.1"

# ── Platform Path & IPC Definitions ───────────────────────────
IS_WINDOWS = sys.platform == "win32"

if IS_WINDOWS:
    USER_CONFIG_DIR = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "ravenspot")
    SERVER_NAME = "ravenspot_ipc_socket"
else:
    USER_CONFIG_DIR = os.path.expanduser("~/.config/ravenspot")
    SERVER_NAME = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ravenspot.sock")

SOCKET_PATH = SERVER_NAME

# PyInstaller bundle path resolution (frozen binary)
if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
    APP_DIR = sys._MEIPASS
    EXE_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
    EXE_DIR = APP_DIR

LEGACY_CONFIG_DIR = USER_CONFIG_DIR
def _resolve_bundled_file(filename):
    ext = os.path.join(EXE_DIR, filename)
    if os.path.isfile(ext):
        return ext
    return os.path.join(APP_DIR, filename)

_WIN_MENU_PATH = _resolve_bundled_file("menu_win.jsonc")
MENU_PATH = _WIN_MENU_PATH if (IS_WINDOWS and os.path.isfile(_WIN_MENU_PATH)) else _resolve_bundled_file("menu.jsonc")
USER_MENU_PATH = os.path.join(USER_CONFIG_DIR, "menu.jsonc")

if IS_WINDOWS:
    import win_platform as wp
else:
    wp = None

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
            pass
        if cmd == "quit":
            sys.exit(0)
    return start_hidden

START_HIDDEN = _try_fast_client()

from PySide6.QtCore import QObject, QUrl, Qt, Slot, QTimer, QSize, Signal, Property, QThread, QProcess, QPoint, QMimeData
from PySide6.QtMultimedia import QSoundEffect
from PySide6.QtGui import QGuiApplication, QIcon, QImage, QCursor, QRegion, QAction, QFontDatabase
from PySide6.QtWidgets import QApplication, QSystemTrayIcon, QMenu, QFileIconProvider
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickImageProvider
from PySide6.QtNetwork import QLocalServer, QLocalSocket


class InlineCommandRunner(QObject):
    """Satır içi komut çalıştırıcı (CommandOutputPanel.qml için Shell.runner)."""

    outputChanged = Signal()
    runningChanged = Signal()
    finished = Signal(int, bool)

    MAX_LINES = 2000

    def __init__(self, parent=None):
        super().__init__(parent)
        self._proc = None
        self._command = ""
        self._lines = []
        self._partial = ""
        self._exit_code = 0
        self._emit_timer = QTimer(self)
        self._emit_timer.setSingleShot(True)
        self._emit_timer.setInterval(50)
        self._emit_timer.timeout.connect(self.outputChanged.emit)

    @Property(bool, notify=runningChanged)
    def running(self):
        return self._proc is not None

    @Property(str, notify=outputChanged)
    def output(self):
        if self._partial:
            return "\n".join(self._lines + [self._partial])
        return "\n".join(self._lines)

    @Property(str, notify=runningChanged)
    def command(self):
        return self._command

    @Property(int, notify=finished)
    def exitCode(self):
        return self._exit_code

    def _append_bytes(self, raw: bytes):
        if not raw:
            return
        text = raw.decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")
        combined = self._partial + text
        parts = combined.split("\n")
        self._partial = parts.pop()
        for ln in parts:
            self._lines.append(ln[:4000])
        if len(self._lines) > self.MAX_LINES:
            self._lines = self._lines[-self.MAX_LINES:]
        if not self._emit_timer.isActive():
            self._emit_timer.start()

    @Slot(str, result=bool)
    def start(self, command):
        if self._proc is not None or not (command or "").strip():
            return False
        self._command = command.strip()
        self._lines = []
        self._partial = ""
        self._exit_code = 0
        proc = QProcess(self)
        proc.setWorkingDirectory(os.path.expanduser("~"))
        proc.setProcessChannelMode(QProcess.MergedChannels)
        proc.readyReadStandardOutput.connect(lambda: self._append_bytes(bytes(proc.readAllStandardOutput())))
        proc.finished.connect(self._on_finished)
        self._proc = proc
        self.runningChanged.emit()
        self.outputChanged.emit()
        if IS_WINDOWS:
            import base64
            ps_script = "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;[Console]::InputEncoding=[System.Text.Encoding]::UTF8;\n" + self._command
            b64 = base64.b64encode(ps_script.encode("utf-16-le")).decode("ascii")
            proc.start("powershell.exe", ["-NoProfile", "-NonInteractive", "-EncodedCommand", b64])
        else:
            proc.start("bash", ["-c", self._command])
        if not proc.waitForStarted(2000):
            proc.kill()
            proc.deleteLater()
            self._proc = None
            self._lines = ["Failed to start shell process."]
            self._exit_code = 1
            self.runningChanged.emit()
            self.outputChanged.emit()
            self.finished.emit(1, True)
            return False
        return True

    def _on_finished(self, exit_code, exit_status):
        proc = self._proc
        if proc is not None:
            self._append_bytes(bytes(proc.readAllStandardOutput()))
            proc.deleteLater()
        self._proc = None
        self._exit_code = int(exit_code)
        crashed = exit_status == QProcess.CrashExit
        self.runningChanged.emit()
        self.outputChanged.emit()
        self.finished.emit(self._exit_code, crashed)

    @Slot()
    def stop(self):
        proc = self._proc
        if proc is None:
            return
        try:
            pid = proc.processId()
            if IS_WINDOWS and pid:
                subprocess.Popen(["taskkill", "/T", "/F", "/PID", str(pid)], **(wp._hidden_kwargs() if wp else {}))
            else:
                proc.terminate()
                if not proc.waitForFinished(1500):
                    proc.kill()
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    @Slot()
    def clear(self):
        self._lines = []
        self._partial = ""
        self.outputChanged.emit()

if not IS_WINDOWS:
    LEGACY_CONFIG_DIR = os.path.join(os.path.expanduser("~"), ".config", "omarchy-menu")
    if not os.path.exists(USER_MENU_PATH) and os.path.exists(os.path.join(LEGACY_CONFIG_DIR, "menu.jsonc")):
        USER_MENU_PATH = os.path.join(LEGACY_CONFIG_DIR, "menu.jsonc")

class Win32HotkeyThread(QThread):
    """Windows global hotkey listener thread (Alt+Space and Win+Space)."""
    hotkeyTriggered = Signal()
    hotkeyWarning = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._running = False
        self._thread_id = None
        self._ready = threading.Event()

    def run(self):
        if not IS_WINDOWS:
            self._ready.set()
            return
        from ctypes import wintypes

        user32 = getattr(ctypes, "windll", None) and getattr(ctypes.windll, "user32", None)
        kernel32 = getattr(ctypes, "windll", None) and getattr(ctypes.windll, "kernel32", None)
        if not user32 or not kernel32:
            self._ready.set()
            return

        MOD_ALT = 0x0001
        MOD_WIN = 0x0008
        MOD_NOREPEAT = 0x4000
        VK_SPACE = 0x20
        WM_HOTKEY = 0x0312
        PM_NOREMOVE = 0x0000

        self._running = True
        try:
            self._thread_id = int(kernel32.GetCurrentThreadId())
        except Exception:
            self._thread_id = None

        # Ensure thread message queue exists before signaling ready (prevents WM_QUIT race)
        msg = wintypes.MSG()
        try:
            user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, PM_NOREMOVE)
        except Exception:
            pass

        # id 1: Alt + Space, id 2: Win + Space
        ok1 = False
        ok2 = False
        try:
            ok1 = bool(user32.RegisterHotKey(None, 1, MOD_ALT | MOD_NOREPEAT, VK_SPACE))
            ok2 = bool(user32.RegisterHotKey(None, 2, MOD_WIN | MOD_NOREPEAT, VK_SPACE))
        except Exception:
            pass
        self._ready.set()
        if not ok1 and not ok2:
            self.hotkeyWarning.emit("Could not register Alt+Space or Win+Space (already in use by another app).")

        try:
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
                if ok1:
                    user32.UnregisterHotKey(None, 1)
                if ok2:
                    user32.UnregisterHotKey(None, 2)
            except Exception:
                pass

    def stop(self):
        self._running = False
        self._ready.wait(timeout=1.0)
        if IS_WINDOWS and self._thread_id:
            try:
                WM_QUIT = 0x0012
                if getattr(ctypes, "windll", None) and getattr(ctypes.windll, "user32", None):
                    ctypes.windll.user32.PostThreadMessageW(self._thread_id, WM_QUIT, 0, 0)
            except Exception:
                pass
        self.wait(1500)

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
    appsChanged = Signal()
    audioConfigChanged = Signal(int, bool)
    powerProfileChanged = Signal(str)
    sleepInhibitedChanged = Signal(bool)
    telemetryUpdated = Signal("QVariantMap")
    fileSearchFinished = Signal(int, str, str)
    windowSearchFinished = Signal(int, str, str)

    def __init__(self, start_hidden=False):
        super().__init__()
        self._start_hidden = start_hidden
        self._win = None
        self._drag_origin_cursor = None
        self._drag_origin_win = None
        self._runner = InlineCommandRunner(self)
        self._runner.finished.connect(self._on_runner_finished)
        self._emoji_entries = None
        self._clip_history = []  # kept in memory only (avoid persisting credentials to disk)
        self._cmd_hist_file = os.path.join(USER_CONFIG_DIR, "command_history.json")
        self._cmd_history = self._load_json_list(self._cmd_hist_file)
        self._file_search_seq = 0
        self._color_picker_win = None
        self._tray = None
        self._win_audio = wp.WinAudioController() if (IS_WINDOWS and wp) else None
        self._win_lhm = wp.WinLhmMiniPoller(APP_DIR) if (IS_WINDOWS and wp) else None
        self._init_extra()
        self._menu_path = MENU_PATH
        self._user_menu_path = USER_MENU_PATH
        self._apps_cache = None  # [ {id,label,icon,aliases,keywords,desktop} ]
        self._win_uwp_cache = None
        self._apps_lock = threading.Lock()
        self._app_watcher = None
        self._linux_fs_watcher = None
        self._linux_app_debounce = None
        self._start_app_watcher()

    def _start_app_watcher(self):
        """Yeni kurulan/kaldırılan uygulamaları sıfır CPU maliyetiyle OS çekirdeğinden anlık izler."""
        if IS_WINDOWS and wp and hasattr(wp, "WinAppIndexWatcher"):
            try:
                self._app_watcher = wp.WinAppIndexWatcher(self._on_apps_fs_changed, debounce_sec=1.5)
                self._app_watcher.start()
            except Exception:
                self._app_watcher = None
            return
        if not IS_WINDOWS:
            try:
                from PySide6.QtCore import QFileSystemWatcher
                dirs = [d for d in _xdg_data_dirs() if os.path.isdir(d)]
                if dirs:
                    self._linux_app_debounce = QTimer(self)
                    self._linux_app_debounce.setSingleShot(True)
                    self._linux_app_debounce.setInterval(1500)
                    self._linux_app_debounce.timeout.connect(
                        lambda: threading.Thread(
                            target=self._on_apps_fs_changed, args=(False,), daemon=True
                        ).start()
                    )
                    self._linux_fs_watcher = QFileSystemWatcher(dirs, self)
                    self._linux_fs_watcher.directoryChanged.connect(lambda _p: self._linux_app_debounce.start())
            except Exception:
                pass

    def _on_apps_fs_changed(self, include_uwp=False):
        """Arka plan iş parçacığında uygulama listesini yeniler; gerçekten değiştiyse QML'i günceller."""
        try:
            new_apps = self._scan_apps(force_uwp=bool(include_uwp))
            with self._apps_lock:
                old = self._apps_cache or []
                old_sig = [(a.get("id"), a.get("label"), a.get("desktop")) for a in old]
                new_sig = [(a.get("id"), a.get("label"), a.get("desktop")) for a in new_apps]
                if old_sig == new_sig:
                    return
                self._apps_cache = new_apps
            self.appsChanged.emit()
        except Exception:
            pass

    def _on_runner_finished(self, exit_code, crashed):
        if self._win is not None and not self._win.isVisible():
            cmd_line = (self._runner.command or "").splitlines()[0][:60]
            status = "Completed" if exit_code == 0 else f"Exited ({exit_code})"
            self.notify(f"Command {status}", cmd_line)

    @staticmethod
    def _load_json_list(path):
        try:
            if os.path.isfile(path):
                with open(path, "r", encoding="utf-8") as f:
                    d = json.load(f)
                    if isinstance(d, list):
                        return d
        except Exception:
            pass
        return []

    @staticmethod
    def _save_json_list(path, items):
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(items, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    @Property(bool, constant=True)
    def startHidden(self):
        return self._start_hidden

    @Property(bool, constant=True)
    def isWindows(self):
        return IS_WINDOWS

    @Property(str, constant=True)
    def iconFontName(self):
        return getattr(self, "_icon_font_name", "") or "FantasqueSansM Nerd Font"

    @Property(str, constant=True)
    def version(self):
        return __version__

    @Property(QObject, constant=True)
    def runner(self):
        return self._runner

    def attachWindow(self, win):
        self._win = win

    def attachTray(self, tray):
        self._tray = tray

    def notify(self, title, body):
        if self._tray is not None:
            try:
                self._tray.showMessage(title, body, QSystemTrayIcon.Information, 3500)
                return
            except Exception:
                pass

    def _on_clipboard_changed(self):
        try:
            cb = QApplication.clipboard()
            if not cb:
                return
            txt = cb.text() or ""
            if not txt.strip():
                return
            if len(txt) > 65536:
                txt = txt[:65536]
            self._clip_history = [x for x in self._clip_history if x != txt]
            self._clip_history.insert(0, txt)
            self._clip_history = self._clip_history[:50]
        except Exception:
            pass

    @Slot(bool)
    def onMenuVisibilityChanged(self, visible):
        if visible:
            self._update_telemetry()
            if hasattr(self, "_telemetry_timer") and not self._telemetry_timer.isActive():
                self._telemetry_timer.start(1000)
            if IS_WINDOWS and wp and self._win is not None:
                try:
                    hwnd = int(self._win.winId())
                    QTimer.singleShot(25, lambda: wp.force_foreground_window(hwnd))
                except Exception:
                    pass
        else:
            if hasattr(self, "_telemetry_timer") and self._telemetry_timer.isActive():
                self._telemetry_timer.stop()

    @Slot()
    def toggleMenu(self):
        self.toggleRequested.emit()

    @Slot()
    def showMenu(self):
        self.showRequested.emit()

    @Slot()
    def hideMenu(self):
        self.hideRequested.emit()

    @Slot(float, float)
    def beginDrag(self, initialDx=0.0, initialDy=0.0):
        if self._win is None:
            return
        self._drag_origin_cursor = QCursor.pos()
        self._drag_origin_win = self._win.position()

    @Slot(float, float, result=QPoint)
    def dragBy(self, deltaX=0.0, deltaY=0.0):
        if self._win is None:
            return QPoint(0, 0)
        if self._drag_origin_cursor is not None and self._drag_origin_win is not None:
            cur = QCursor.pos()
            nx = self._drag_origin_win.x() + (cur.x() - self._drag_origin_cursor.x())
            ny = self._drag_origin_win.y() + (cur.y() - self._drag_origin_cursor.y())
            self._win.setPosition(int(nx), int(ny))
            return QPoint(int(nx), int(ny))
        pos = self._win.position()
        nx, ny = int(pos.x() + deltaX), int(pos.y() + deltaY)
        self._win.setPosition(nx, ny)
        return QPoint(nx, ny)

    @Slot()
    def endDrag(self):
        self._drag_origin_cursor = None
        self._drag_origin_win = None

    @Slot(int)
    def setCardHeight(self, height):
        # On Windows transparent QQuickWindow handles per-pixel alpha hit-testing cleanly
        # without re-allocating Win32 HRGN on every animation frame (prevents shadow clipping).
        return

    @Slot(result=bool)
    def isSwitchingScreen(self):
        return False

    @Slot(result=bool)
    def perfLogging(self):
        return bool(os.environ.get("RAVENSPOT_PERF"))

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
        if IS_WINDOWS:
            return ""
        try:
            p = subprocess.run(
                ["bash", "-c", script],
                capture_output=True, text=True, timeout=20,
            )
            return p.stdout or ""
        except Exception:
            return ""

    @Slot(str)
    def runDetached(self, command):
        """Eylemi arka planda, menü kapanınca ölmeden çalıştır."""
        if not command or not command.strip():
            return
        cmd = command.strip()
        if IS_WINDOWS:
            # Built-in RavenSpot internal actions
            if cmd == "ravenspot:pick-color":
                self.pickColor()
                return
            if cmd == "ravenspot:lock":
                self.lockScreen()
                return
            if cmd == "ravenspot:suspend":
                self.suspendSystem()
                return
            if cmd == "ravenspot:logout":
                self.logoutUser()
                return
            if cmd == "ravenspot:reboot":
                self.rebootSystem()
                return
            if cmd == "ravenspot:reboot-bios":
                self.rebootToBios()
                return
            if cmd == "ravenspot:shutdown":
                self.powerOff()
                return
            if cmd == "ravenspot:terminal":
                self.openInTerminal(os.path.expanduser("~"))
                return
            if cmd == "ravenspot:screenshot-full":
                QTimer.singleShot(250, self._take_windows_fullscreen_screenshot)
                return
            if cmd == "ravenspot:shortcuts-help":
                self.notify(
                    "RavenSpot Shortcuts",
                    "Alt+Space / Win+Space: Toggle • >: Shell • *: Files • :emoji • clip • kill",
                )
                return
            expanded = os.path.expandvars(cmd)
            if expanded.lower() == "ms-screenclip:":
                # Wait for closeTimer (90ms) so RavenSpot hides before Snipping overlay freezes screen
                QTimer.singleShot(250, lambda: os.startfile("ms-screenclip:"))
                return
            if re.match(r"^(ms-settings:|ms-windows-store:|ms-screenclip:|steam:|https?://|mailto:)", expanded, re.I):
                try:
                    os.startfile(expanded)
                except Exception:
                    ctypes.windll.shell32.ShellExecuteW(None, "open", expanded, None, None, 1)
                return
            if expanded.lower().startswith("explorer.exe "):
                arg = expanded[len("explorer.exe "):].strip().strip('"')
                subprocess.Popen(["explorer.exe", os.path.normpath(arg)])
                return
            if os.path.exists(expanded):
                try:
                    os.startfile(expanded)
                    return
                except Exception:
                    pass
            # Direct executable lookup (e.g. taskmgr.exe, snippingtool.exe) without PowerShell overhead
            exe_hit = shutil.which(expanded)
            if exe_hit:
                try:
                    os.startfile(exe_hit)
                    return
                except Exception:
                    subprocess.Popen([exe_hit])
                    return
            try:
                import base64
                b64 = base64.b64encode(expanded.encode("utf-16-le")).decode("ascii")
                subprocess.Popen(
                    ["powershell.exe", "-NoProfile", "-WindowStyle", "Hidden", "-EncodedCommand", b64],
                    cwd=os.path.expanduser("~"),
                    **(wp._hidden_kwargs() if wp else {}),
                )
            except Exception:
                pass
            return

        subprocess.Popen(
            ["bash", "-c", cmd],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,  # menü kapanınca SIGHUP almasın
        )

    def _take_windows_fullscreen_screenshot(self):
        try:
            pics = self.getFolderPath("pictures") or os.path.expanduser("~")
            out_dir = os.path.join(pics, "Screenshots")
            os.makedirs(out_dir, exist_ok=True)
            import datetime
            fname = datetime.datetime.now().strftime("Screenshot_%Y-%m-%d_%H-%M-%S.png")
            out_path = os.path.join(out_dir, fname)
            screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
            if screen:
                pm = screen.grabWindow(0)
                pm.save(out_path, "PNG")
                QApplication.clipboard().setPixmap(pm)
                self.notify("Screenshot Saved", f"Copied & saved to {fname}")
        except Exception:
            pass

    @Slot(str)
    @Slot(str, bool)
    def executeCommand(self, command, in_terminal=False):
        """Komutu arka planda veya terminalde çalıştır."""
        if in_terminal:
            self.runCommandInTerminal(command)
        else:
            self.runDetached(command)

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

    def _scan_apps(self, force_uwp=False):
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
            if wp:
                if force_uwp or self._win_uwp_cache is None:
                    self._win_uwp_cache = wp.scan_windows_uwp_apps()
                for app in (self._win_uwp_cache or []):
                    k = app["label"].lower()
                    if k not in seen:
                        seen.add(k)
                        apps.append(app)
            apps.sort(key=lambda a: a["label"].lower())
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
            is_kcm = name.startswith("kcm_")
            if de.get("NoDisplay", "false").lower() == "true" and not is_kcm:
                continue
            if de.get("Hidden", "false").lower() == "true":
                continue

            # Bilingual support for label & aliases
            label_tr = de.get("Name[tr]", "")
            label_en = de.get("Name", "")
            label = label_tr if label_tr else label_en
            if not label:
                label = name[:-8]

            aliases = []
            if label_en and label_en != label:
                aliases.append(label_en)
            if label_tr and label_tr != label:
                aliases.append(label_tr)
            gn = de.get("GenericName", "")
            if gn:
                aliases.append(gn)
            gn_tr = de.get("GenericName[tr]", "")
            if gn_tr:
                aliases.append(gn_tr)
            kw = de.get("Keywords", "")
            if kw:
                aliases.extend(k.strip() for k in kw.replace(";", " ").split() if k.strip())
            kw_tr = de.get("Keywords[tr]", "")
            if kw_tr:
                aliases.extend(k.strip() for k in kw_tr.replace(";", " ").split() if k.strip())
            comment = de.get("Comment", "")
            if comment:
                aliases.append(comment)
            comment_tr = de.get("Comment[tr]", "")
            if comment_tr:
                aliases.append(comment_tr)
            if is_kcm:
                aliases.extend(["settings", "ayarlar", "setup", "kcm", "systemsettings"])
                cat = "system"
            else:
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
        return apps

    def _load_apps(self):
        with self._apps_lock:
            if self._apps_cache is not None:
                return self._apps_cache
        apps = self._scan_apps(force_uwp=True)
        with self._apps_lock:
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
            "system": 0, "utilities": 0, "other": 0, "all": len(all_apps)
        }
        for a in all_apps:
            cat = a.get("category", "utilities")
            if cat in counts:
                counts[cat] += 1
            else:
                counts["other"] += 1
        return json.dumps(counts)

    @Slot(str)
    def launchApp(self, desktop):
        """Bir uygulamayı / kısayolu başlat."""
        if not desktop:
            return
        if IS_WINDOWS:
            if desktop.lower().startswith("shell:"):
                subprocess.Popen(["explorer.exe", desktop])
                return
            try:
                os.startfile(desktop)
            except Exception:
                subprocess.Popen(["cmd.exe", "/c", "start", "", desktop], **(wp._hidden_kwargs() if wp else {}))
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
        if IS_WINDOWS and wp:
            return json.dumps(wp.list_windows_color_modes())
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
        if IS_WINDOWS and wp:
            wp.apply_windows_color_mode(name)
            return
        subprocess.Popen(
            ["plasma-apply-colorscheme", name],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: power-profiles ─────────────────────────────────
    @Slot(result=str)
    def listPowerProfiles(self):
        if IS_WINDOWS and wp:
            return json.dumps(wp.list_windows_power_profiles())
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
        if IS_WINDOWS and wp:
            if wp.set_windows_power_profile(profile):
                self.powerProfileChanged.emit(wp.get_windows_power_profile())
            return
        subprocess.Popen(
            ["powerprofilesctl", "set", profile],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

    # ── Provider: clipboard ───────────────────────────────────────
    def _clip_rows(self, query=""):
        ql = (query or "").strip().lower()
        rows = []
        # Also include current clipboard text if not yet captured
        try:
            cb = QApplication.clipboard()
            cur = (cb.text() or "") if cb else ""
            if cur.strip() and (not self._clip_history or self._clip_history[0] != cur):
                self._clip_history = [cur] + [x for x in self._clip_history if x != cur]
                self._clip_history = self._clip_history[:50]
        except Exception:
            pass
        for item in self._clip_history:
            if ql and ql not in item.lower():
                continue
            display = " ".join(item.split())
            if len(display) > 64:
                display = display[:61] + "..."
            rows.append({
                "label": display,
                "value": item[:4096],
                "truncated": len(item) > 4096,
            })
            if len(rows) >= 30:
                break
        return rows

    @Slot(result=str)
    def listClipboard(self):
        if IS_WINDOWS:
            return json.dumps(self._clip_rows(""))
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

    @Slot(str, result=str)
    def clipboardMatching(self, query):
        if IS_WINDOWS:
            return json.dumps(self._clip_rows(query))
        try:
            all_rows = json.loads(self.listClipboard() or "[]")
            ql = (query or "").strip().lower()
            if not ql:
                return json.dumps(all_rows)
            return json.dumps([r for r in all_rows if ql in (r.get("value") or "").lower()])
        except Exception:
            return "[]"

    @Slot(str)
    def setClipboard(self, text):
        if not text:
            return
        if IS_WINDOWS:
            try:
                cb = QApplication.clipboard()
                if cb:
                    cb.setText(text)
            except Exception:
                pass
            return
        try:
            subprocess.run(
                ["qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.setClipboardContents", text],
                timeout=1,
            )
        except Exception:
            pass

    @Slot(str)
    def copyToClipboard(self, text):
        self.setClipboard(text)

    # ── Provider: files (downloads / user dirs) ───────────────────
    @Slot(str, result=str)
    def getFolderPath(self, folder_name="downloads"):
        """Kullanıcının standart dizinlerinden birinin gerçek yolunu döndür (OneDrive yönlendirmeleri dahil)."""
        key = (folder_name or "downloads").lower()
        if IS_WINDOWS:
            try:
                from PySide6.QtCore import QStandardPaths
                sp_map = {
                    "downloads": QStandardPaths.DownloadLocation,
                    "desktop": QStandardPaths.DesktopLocation,
                    "documents": QStandardPaths.DocumentsLocation,
                    "pictures": QStandardPaths.PicturesLocation,
                    "videos": QStandardPaths.MoviesLocation,
                    "music": QStandardPaths.MusicLocation,
                }
                if key in sp_map:
                    loc = QStandardPaths.writableLocation(sp_map[key])
                    if loc and os.path.isdir(loc):
                        return os.path.normpath(loc)
            except Exception:
                pass
        home = os.path.expanduser("~")
        mapping = {
            "downloads": ["İndirilenler", "Downloads"],
            "desktop": ["Masaüstü", "Desktop"],
            "documents": ["Belgeler", "Documents"],
            "pictures": ["Resimler", "Pictures"],
            "videos": ["Videolar", "Videos"],
            "music": ["Müzik", "Music"],
            "wallpapers": [".local/share/wallpapers", "Resimler/Wallpapers", "Pictures/Wallpapers", "Wallpapers"],
        }
        for candidate in mapping.get(key, [folder_name]):
            p = os.path.join(home, candidate) if not os.path.isabs(candidate) else candidate
            if os.path.isdir(p):
                return p
        p = os.path.join(home, folder_name)
        return p if os.path.isdir(p) else ""

    @Slot(str, result=bool)
    def isDirectory(self, path):
        """Yolun bir klasör olup olmadığını kontrol et."""
        if not path:
            return False
        return os.path.isdir(os.path.expanduser(path))

    @Slot(str, str, str, str, result=str)
    def resolveItemPath(self, item_id="", provider="", action="", value=""):
        """Herhangi bir menü öğesi veya eylemin temsil ettiği dosya/klasör yolunu tespit et."""
        if value:
            exp = os.path.expanduser(value)
            if os.path.exists(exp):
                return exp

        if provider and provider.startswith("files-"):
            folder_key = provider[6:]
            p = self.getFolderPath(folder_key)
            if p:
                return p

        known_folders = ["downloads", "desktop", "documents", "pictures", "videos", "music", "wallpapers"]
        low_id = item_id.lower() if item_id else ""
        for k in known_folders:
            if low_id == k or low_id.endswith("." + k) or low_id.startswith(k + "."):
                p = self.getFolderPath(k)
                if p:
                    return p

        if action:
            import re
            matches = re.findall(r'(?:~|/)[^\s\'";]+', action)
            for m in matches:
                exp = os.path.expanduser(m)
                if os.path.exists(exp):
                    return exp

        # 5. Apps listesi içinde desktop kontrolü (uygulama konumu)
        apps_list = self._load_apps()
        if item_id:
            for a in apps_list:
                aid = (a.get("id") or "").lower()
                if aid == low_id or aid.endswith("." + low_id):
                    return a.get("desktop", "")
        if action:
            act_cmd = action.split()[0].lower() if action.split() else ""
            for a in apps_list:
                aid = (a.get("id") or "").lower()
                if aid == act_cmd or aid.endswith("." + act_cmd):
                    return a.get("desktop", "")

        return ""

    @Slot(str, result=str)
    def listFolderFiles(self, folder_name="downloads"):
        import datetime, mimetypes
        if IS_WINDOWS and wp and (folder_name or "").lower() == "steam":
            return json.dumps(wp.list_windows_steam_games())
        target_dir = self.getFolderPath(folder_name)
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
                    sz_str = "Folder" if is_d else _fmt_size(st.st_size)
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
            raw = path.strip()
            if re.match(r"^(https?://|mailto:|steam:|ms-settings:|ms-windows-store:|ms-screenclip:)", raw, re.I):
                if IS_WINDOWS:
                    try:
                        os.startfile(raw)
                    except Exception:
                        ctypes.windll.shell32.ShellExecuteW(None, "open", raw, None, None, 1)
                else:
                    subprocess.Popen(["xdg-open", raw], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
                return
            expanded = os.path.expandvars(os.path.expanduser(raw))
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
    def searchFiles(self, pattern):
        """Everything tarzı anlık dosya ve dizin arama."""
        pattern = pattern.strip()
        if not pattern:
            return "[]"

        results = []
        seen = set()
        limit = 35

        # 1. Linux: fd in $HOME (blazing fast, fresh)
        if not IS_WINDOWS and shutil.which("fd"):
            try:
                cmd = [
                    "fd", "-i", "-a", "--max-results", str(limit),
                    "--exclude", ".git", "--exclude", "node_modules",
                    "--exclude", ".cache", "--exclude", ".local/share/Trash",
                    pattern, os.path.expanduser("~")
                ]
                p = subprocess.run(cmd, capture_output=True, text=True, timeout=1.5)
                for line in p.stdout.splitlines():
                    path = line.strip()
                    if path and path not in seen:
                        seen.add(path)
                        results.append(path)
            except Exception:
                pass

        # 2. Linux: plocate fallback for system-wide / fast disk indexing
        if not IS_WINDOWS and len(results) < limit and shutil.which("plocate"):
            try:
                cmd = ["plocate", "-i", "-l", str(limit - len(results)), pattern]
                p = subprocess.run(cmd, capture_output=True, text=True, timeout=1.0)
                for line in p.stdout.splitlines():
                    path = line.strip()
                    if path and path not in seen:
                        seen.add(path)
                        results.append(path)
            except Exception:
                pass

        # 3. Windows fallback
        if IS_WINDOWS:
            es_exe = shutil.which("es.exe") or shutil.which("es")
            if es_exe:
                try:
                    cmd = [es_exe, "-n", str(limit), "--", pattern]
                    p = subprocess.run(
                        cmd, capture_output=True, text=True, encoding="oem", errors="replace",
                        timeout=1.5, **(wp._hidden_kwargs() if wp else {})
                    )
                    for line in p.stdout.splitlines():
                        path = line.strip()
                        if path and path not in seen:
                            seen.add(path)
                            results.append(path)
                except Exception:
                    pass
            if not results:
                user_dirs = []
                for fk in ("documents", "downloads", "desktop", "pictures", "videos", "music"):
                    pdir = self.getFolderPath(fk)
                    if pdir and pdir not in user_dirs:
                        user_dirs.append(pdir)
                pat_lower = wp.fold_text(pattern) if wp else pattern.lower()
                skip_dirs = {".git", "node_modules", "__pycache__", ".venv", "venv", "appdata", "$recycle.bin"}
                for u_dir in user_dirs:
                    if len(results) >= limit or not os.path.isdir(u_dir):
                        break
                    try:
                        for root_dir, dirs, files in os.walk(u_dir):
                            dirs[:] = [d for d in dirs if d.lower() not in skip_dirs]
                            for fn in files + dirs:
                                fn_cmp = wp.fold_text(fn) if wp else fn.lower()
                                if pat_lower in fn_cmp:
                                    full_p = os.path.join(root_dir, fn)
                                    if full_p not in seen:
                                        seen.add(full_p)
                                        results.append(full_p)
                                        if len(results) >= limit:
                                            break
                            if len(results) >= limit:
                                break
                    except Exception:
                        pass

        import datetime
        items = []
        home_dir = os.path.expanduser("~")
        for full_path in results:
            try:
                is_dir = os.path.isdir(full_path)
                basename = os.path.basename(full_path.rstrip(os.sep))
                dir_path = os.path.dirname(full_path)
                short_dir = dir_path.replace(home_dir, "~") if home_dir else dir_path

                size_str = ""
                mod_str = ""
                try:
                    st = os.stat(full_path)
                    if not is_dir:
                        sz = st.st_size
                        for unit in ["B", "KB", "MB", "GB"]:
                            if sz < 1024.0:
                                size_str = f"{sz:.1f} {unit}" if unit != "B" else f"{int(sz)} B"
                                break
                            sz /= 1024.0
                        else:
                            size_str = f"{sz:.1f} TB"
                    dt = datetime.datetime.fromtimestamp(st.st_mtime)
                    mod_str = dt.strftime("%d.%m.%Y %H:%M")
                except OSError:
                    pass

                # Icon determination
                if is_dir:
                    icon_name = "folder"
                    icon_glyph = "󰝰"
                else:
                    ext = os.path.splitext(basename)[1].lower()
                    if ext in (".png", ".jpg", ".jpeg", ".webp", ".svg", ".gif", ".ico", ".bmp"):
                        icon_name = "image-x-generic"
                        icon_glyph = "󰋩"
                    elif ext in (".mp4", ".mkv", ".mov", ".avi", ".webm", ".flv"):
                        icon_name = "video-x-generic"
                        icon_glyph = "󰕼"
                    elif ext in (".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac"):
                        icon_name = "audio-x-generic"
                        icon_glyph = "󰕾"
                    elif ext in (".pdf",):
                        icon_name = "application-pdf"
                        icon_glyph = "󰈦"
                    elif ext in (".zip", ".tar", ".gz", ".xz", ".7z", ".rar", ".bz2", ".zst"):
                        icon_name = "package-x-generic"
                        icon_glyph = "󰛫"
                    elif ext in (".doc", ".docx", ".odt", ".rtf"):
                        icon_name = "application-msword"
                        icon_glyph = "󰈬"
                    elif ext in (".xls", ".xlsx", ".ods", ".csv"):
                        icon_name = "application-vnd.ms-excel"
                        icon_glyph = "󰈛"
                    elif ext in (".ppt", ".pptx", ".odp"):
                        icon_name = "application-vnd.ms-powerpoint"
                        icon_glyph = "󰈧"
                    elif ext in (".py", ".js", ".ts", ".html", ".css", ".cpp", ".c", ".rs", ".json", ".qml", ".sh", ".bash"):
                        icon_name = "text-x-generic"
                        icon_glyph = "󰅩"
                    elif ext in (".desktop",):
                        icon_name = "system-run"
                        icon_glyph = "󰀻"
                    else:
                        icon_name = "text-plain"
                        icon_glyph = "󰈙"

                items.append({
                    "name": basename,
                    "path": full_path,
                    "dir": short_dir,
                    "isDir": is_dir,
                    "size": size_str,
                    "date": mod_str,
                    "icon": icon_glyph,
                    "iconName": icon_name
                })
            except Exception:
                pass

        return json.dumps(items)

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
                norm_dir = os.path.normpath(target_dir) if target_dir and os.path.isdir(target_dir) else os.path.expanduser("~")
                wt_exe = shutil.which("wt.exe") or shutil.which("wt")
                if wt_exe:
                    subprocess.Popen([wt_exe, "-d", norm_dir])
                elif shutil.which("powershell.exe") or shutil.which("powershell"):
                    subprocess.Popen(["powershell.exe", "-NoExit"], cwd=norm_dir)
                else:
                    subprocess.Popen(["cmd.exe", "/K"], cwd=norm_dir)
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
                import base64
                ps_cmd = "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;\n" + cmd
                b64 = base64.b64encode(ps_cmd.encode("utf-16-le")).decode("ascii")
                wt_exe = shutil.which("wt.exe") or shutil.which("wt")
                home_dir = os.path.expanduser("~")
                if wt_exe:
                    subprocess.Popen([wt_exe, "-d", home_dir, "--", "powershell.exe", "-NoProfile", "-NoExit", "-EncodedCommand", b64])
                elif shutil.which("powershell.exe") or shutil.which("powershell"):
                    subprocess.Popen(["powershell.exe", "-NoProfile", "-NoExit", "-EncodedCommand", b64], cwd=home_dir)
                else:
                    subprocess.Popen(["cmd.exe", "/K", cmd], cwd=home_dir)
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
                if desktop.lower().startswith("shell:"):
                    return
                norm = os.path.normpath(desktop)
                if norm.lower().endswith(".lnk"):
                    # Open native Windows Shortcut Properties dialog (never open binary .lnk in Notepad)
                    ctypes.windll.shell32.ShellExecuteW(None, "properties", norm, None, None, 1)
                elif shutil.which("notepad.exe") or shutil.which("notepad"):
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
                    FOF_NOERRORUI = 0x0400

                    # create_unicode_buffer appends a NUL, so adding one '\0' guarantees double-NUL termination
                    buf = ctypes.create_unicode_buffer(os.path.normpath(expanded) + "\0")
                    fileop = SHFILEOPSTRUCTW()
                    fileop.wFunc = FO_DELETE
                    fileop.pFrom = ctypes.cast(buf, wintypes.LPCWSTR)
                    fileop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI
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
    appAudioChanged = Signal(int, bool)

    def _init_extra(self):
        self._themes_file = _resolve_bundled_file("themes.json")
        self._user_theme_file = os.path.join(USER_CONFIG_DIR, "theme.json")
        os.makedirs(os.path.dirname(self._user_theme_file), exist_ok=True)
        
        # Load themes
        with open(self._themes_file, encoding="utf-8") as f:
            self._themes_data = json.load(f)
        if IS_WINDOWS and "win11_fluent" in self._themes_data.get("themes", {}):
            self._themes_data["active"] = "win11_fluent"
        
        # Read saved user choice
        if os.path.exists(self._user_theme_file):
            try:
                with open(self._user_theme_file, encoding="utf-8") as f:
                    saved = json.load(f)
                    if saved.get("active") in self._themes_data["themes"]:
                        self._themes_data["active"] = saved["active"]
            except Exception:
                pass

        # Apply current theme icon theme to QIcon & search paths
        local_icons_dir = os.path.join(APP_DIR, "icons")
        if os.path.isdir(local_icons_dir):
            paths = [local_icons_dir] + [p for p in QIcon.themeSearchPaths() if p != local_icons_dir]
            QIcon.setThemeSearchPaths(paths)
        cur_icon_theme = self.currentTheme().get("iconTheme", "candy-icons")
        QIcon.setThemeName(cur_icon_theme)

        # Sounds & App Volume (Aula AU75 / F75 creamy switch profile with persistent app volume)
        self._sleep_inhibit_cookie = None
        self._user_sound_file = os.path.join(USER_CONFIG_DIR, "sound.json")
        self._app_volume = 5
        self._app_muted = False
        if os.path.exists(self._user_sound_file):
            try:
                with open(self._user_sound_file, "r", encoding="utf-8") as f:
                    s_data = json.load(f)
                    self._app_volume = max(0, min(100, int(s_data.get("volume", 65))))
                    self._app_muted = bool(s_data.get("muted", False))
            except Exception:
                pass

        eff_vol = 0.0 if self._app_muted else (self._app_volume / 100.0)
        self._snd_thock_pool = []
        for _ in range(4):
            snd = QSoundEffect(self)
            snd.setSource(QUrl.fromLocalFile(os.path.join(APP_DIR, "sounds", "thock.wav")))
            snd.setVolume(0.28 * eff_vol)
            self._snd_thock_pool.append(snd)
        self._snd_thock_idx = 0
        self._snd_thock = self._snd_thock_pool[0]

        self._snd_creamy = QSoundEffect(self)
        self._snd_creamy.setSource(QUrl.fromLocalFile(os.path.join(APP_DIR, "sounds", "creamy.wav")))
        self._snd_creamy.setVolume(0.32 * eff_vol)

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
        if IS_WINDOWS and self._win_lhm and self._win_lhm.available:
            gpu_pct = int(self._win_lhm.data.get("gpu_load") or 0)
        elif not IS_WINDOWS:
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
        if IS_WINDOWS and self._win_audio:
            vol_pct, is_muted = self._win_audio.get_volume_info()
        elif not IS_WINDOWS:
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

                up_sec = wp.get_uptime_seconds() if wp else int(ctypes.windll.kernel32.GetTickCount64() // 1000)
                up_hrs = up_sec // 3600
                up_mins = (up_sec % 3600) // 60
                up_text = f"{up_hrs}h {up_mins}m"
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
                up_text = f"{up_hrs}h {up_mins}m"
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
        if IS_WINDOWS and self._win_lhm and self._win_lhm.available:
            temp_c = int(self._win_lhm.data.get("cpu_temp") or 0)
        elif not IS_WINDOWS:
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

    @Slot(result="QVariantMap")
    def getUpdateInfo(self):
        if IS_WINDOWS:
            return {
                "has_updates": False,
                "count": 0,
                "icon_path": "image://themeicon/system-software-update",
                "tooltip": "Windows Update — Click to Check"
            }
        """
        CachyOS / Arch güncelleme durumunu döndürür.
        Photo 2: cachy-update_updates-available-blue.svg (Kırmızı ok + güncelleme adedi)
        Photo 3: cachy-update-blue.svg (Yeşil tik + sistem güncel)
        """
        statedir = os.path.expanduser("~/.local/state/arch-update")
        tray_file = os.path.join(statedir, "tray_icon")
        updates_file = os.path.join(statedir, "last_updates_check")
        notif_file = f"/run/user/{os.getuid()}/arch-update/notif_param" if not IS_WINDOWS else ""

        has_updates = False
        count = 0
        tray_val = ""

        if os.path.exists(tray_file):
            try:
                with open(tray_file, "r") as f:
                    tray_val = f.read().strip()
                    if "updates-available" in tray_val:
                        has_updates = True
            except Exception:
                pass

        if os.path.exists(updates_file):
            try:
                with open(updates_file, "r") as f:
                    lines = [line.strip() for line in f if line.strip()]
                    count = len(lines)
                    if count > 0:
                        has_updates = True
            except Exception:
                pass
        elif has_updates and notif_file and os.path.exists(notif_file):
            try:
                with open(notif_file, "r") as f:
                    c = f.read().strip()
                    if c.isdigit():
                        count = int(c)
            except Exception:
                pass

        if has_updates:
            icon_name = tray_val if "updates-available" in tray_val else "cachy-update_updates-available-blue"
            icon_path = f"/usr/share/icons/hicolor/scalable/apps/{icon_name}.svg"
            if not os.path.exists(icon_path):
                icon_path = "/usr/share/icons/hicolor/scalable/apps/cachy-update_updates-available-blue.svg"
            cnt_str = f" ({count})" if count > 0 else ""
            tooltip = f"Updates Available{cnt_str} — Click to Update"
        else:
            icon_name = tray_val if tray_val and "updates-available" not in tray_val else "cachy-update-blue"
            icon_path = f"/usr/share/icons/hicolor/scalable/apps/{icon_name}.svg"
            if not os.path.exists(icon_path):
                icon_path = "/usr/share/icons/hicolor/scalable/apps/cachy-update-blue.svg"
            tooltip = "System Up to Date (CachyOS)"

        return {
            "has_updates": has_updates,
            "count": count,
            "icon_path": icon_path,
            "tooltip": tooltip
        }

    @Slot()
    def launchUpdate(self):
        """Güncelleme aracını ayrık (detached) çalıştır."""
        if IS_WINDOWS:
            self.runDetached("ms-settings:windowsupdate")
            return
        for term in ["konsole", "alacritty", "kitty", "xterm"]:
            if shutil.which(term):
                if shutil.which("arch-update"):
                    subprocess.Popen([term, "-e", "arch-update"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return
                elif shutil.which("cachyos-rate-mirrors"):
                    subprocess.Popen([term, "-e", "bash", "-c", "sudo pacman -Syu"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return

    @Slot(result=str)
    def getPowerProfile(self):
        """KDE / Linux veya Windows aktif güç profilini döndürür."""
        if IS_WINDOWS and wp:
            return wp.get_windows_power_profile()
        try:
            res = subprocess.run(["powerprofilesctl", "get"], capture_output=True, text=True, check=True, timeout=2)
            return res.stdout.strip()
        except Exception:
            return "balanced"

    @Slot(str, result=bool)
    def setPowerProfile(self, profile: str):
        """Sistem güç profilini ayarlar (power-saver, balanced, performance)."""
        if IS_WINDOWS and wp:
            ok = wp.set_windows_power_profile(profile)
            if ok:
                self.powerProfileChanged.emit(wp.get_windows_power_profile())
            return ok
        if profile not in ("power-saver", "balanced", "performance"):
            return False
        try:
            subprocess.run(["powerprofilesctl", "set", profile], check=True, timeout=2)
            return True
        except Exception as e:
            print(f"Error setting power profile: {e}")
            return False

    @Slot(result=bool)
    def isSleepInhibited(self):
        """RavenSpot tarafından manuel uyku ve ekran kilidi engelleme aktif mi?"""
        return self._sleep_inhibit_cookie is not None

    @Slot(bool, result=bool)
    def setSleepInhibited(self, inhibit: bool):
        """Uykuyu ve Ekran Kilidini Elle Engelle (Inhibit / UnInhibit) açıp kapatır."""
        if IS_WINDOWS and wp:
            ok = wp.set_sleep_inhibited(bool(inhibit))
            self._sleep_inhibit_cookie = 1 if (inhibit and ok) else None
            self.sleepInhibitedChanged.emit(self._sleep_inhibit_cookie is not None)
            return self._sleep_inhibit_cookie is not None
        if inhibit:
            if self._sleep_inhibit_cookie is not None:
                return True
            try:
                res = subprocess.run(
                    ["qdbus6", "org.freedesktop.PowerManagement.Inhibit",
                     "/org/freedesktop/PowerManagement/Inhibit",
                     "org.freedesktop.PowerManagement.Inhibit.Inhibit",
                     "RavenSpot", "Manually Block Sleep and Screen Locking"],
                    capture_output=True, text=True, check=True, timeout=2
                )
                cookie = int(res.stdout.strip())
                self._sleep_inhibit_cookie = cookie
                return True
            except Exception as e:
                print(f"Error inhibiting sleep: {e}")
                return False
        else:
            if self._sleep_inhibit_cookie is not None:
                try:
                    subprocess.run(
                        ["qdbus6", "org.freedesktop.PowerManagement.Inhibit",
                         "/org/freedesktop/PowerManagement/Inhibit",
                         "org.freedesktop.PowerManagement.Inhibit.UnInhibit",
                         str(self._sleep_inhibit_cookie)],
                        check=True, timeout=2
                    )
                except Exception as e:
                    print(f"Error un-inhibiting sleep: {e}")
                self._sleep_inhibit_cookie = None
            return False

    def cleanup(self):
        """Uygulama sonlanırken engellemeleri serbest bırak."""
        if self._app_watcher is not None:
            self._app_watcher.stop()
        if self._win_lhm is not None:
            self._win_lhm.stop()
        if self._runner is not None:
            self._runner.stop()
        if self._sleep_inhibit_cookie is not None:
            if IS_WINDOWS and wp:
                wp.set_sleep_inhibited(False)
            else:
                try:
                    subprocess.run(
                        ["qdbus6", "org.freedesktop.PowerManagement.Inhibit",
                         "/org/freedesktop/PowerManagement/Inhibit",
                         "org.freedesktop.PowerManagement.Inhibit.UnInhibit",
                         str(self._sleep_inhibit_cookie)],
                        timeout=1
                    )
                except Exception:
                    pass
            self._sleep_inhibit_cookie = None

    @Slot(str)
    def openSystemMonitor(self, section: str = "overview"):
        """
        Sistem izleme penceresini ilgili sekmede açar (GPU, CPU, RAM, Disk, Isı vb.).
        Kullanıcı hızını maksimize etmek için ayrık (detached) ve gecikmesiz çalıştırır.
        """
        section = (section or "overview").strip().lower()
        print(f"ravenspot: openSystemMonitor çağrıldı -> {section}", flush=True)

        # 0. Windows: open Task Manager / Resource Monitor directly (before checking Linux CLI tools)
        if IS_WINDOWS:
            target = "resmon.exe" if section in ("ram", "disk") else "taskmgr.exe"
            try:
                os.startfile(target)
            except Exception:
                subprocess.Popen([target])
            return

        # 1. GPU için özel GUI araçları varsa (LACT, amdgpu_top, CoreCtrl, NVIDIA Settings vb.)
        if section in ("gpu", "ekrankarti", "ekran-karti"):
            if shutil.which("lact"):
                subprocess.Popen(["lact", "gui"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            for gpu_tool in ["lact-gui", "corectrl", "nvidia-settings"]:
                if shutil.which(gpu_tool):
                    subprocess.Popen([gpu_tool], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return
            if shutil.which("amdgpu_top"):
                subprocess.Popen(["amdgpu_top", "--gui"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return

        # 2. CPU için özel donanım analiz araçları (CPU-X, Mission Center)
        if section in ("cpu", "islemci"):
            for cpu_tool in ["cpu-x", "mission-center"]:
                if shutil.which(cpu_tool):
                    subprocess.Popen([cpu_tool], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return

        # 3. RAM için özel donanım analiz araçları (Mission Center, CPU-X)
        if section in ("ram", "bellek"):
            for ram_tool in ["mission-center", "cpu-x"]:
                if shutil.which(ram_tool):
                    subprocess.Popen([ram_tool], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return

        # 2. KDE Plasma Sistem Monitörü (plasma-systemmonitor)
        if shutil.which("plasma-systemmonitor"):
            page_id = "overview.page"
            if section in ("gpu", "cpu", "ram", "temp", "isi"):
                page_id = "history.page"
            elif section in ("proc", "process", "processes"):
                page_id = "processes.page"
            elif section in ("app", "apps", "applications"):
                page_id = "applications.page"
            elif section in ("disk", "uptime", "sure"):
                page_id = "overview.page"

            subprocess.Popen(
                ["plasma-systemmonitor", "--page-id", page_id],
                start_new_session=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            return

        # 3. KInfoCenter modülleri
        if section in ("temp", "isi") and shutil.which("kinfocenter"):
            subprocess.Popen(["kinfocenter", "kcm_sensors"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return

        # 4. Alternatif GUI/Terminal monitörleri (btop, htop)
        for term in ["konsole", "alacritty", "kitty", "xterm"]:
            if shutil.which(term) and shutil.which("btop"):
                subprocess.Popen([term, "-e", "btop"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            elif shutil.which(term) and shutil.which("htop"):
                subprocess.Popen([term, "-e", "htop"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return

        # 5. Windows için Task Manager / Resource Monitor
        if IS_WINDOWS:
            if section in ("ram", "disk"):
                subprocess.Popen(["resmon.exe"])
            else:
                subprocess.Popen(["taskmgr.exe"])
            return

        # Fallback: kinfocenter
        if shutil.which("kinfocenter"):
            subprocess.Popen(["kinfocenter"], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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
        if qquick_window:
            self._win = qquick_window
        target_screen = None
        if not IS_WINDOWS:
            out_name = self.getActiveOutputName()
            for s in QGuiApplication.screens():
                if s.name() == out_name:
                    target_screen = s
                    break
        if not target_screen:
            target_screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
        if target_screen and qquick_window:
            geom = target_screen.geometry()
            if qquick_window.screen() != target_screen:
                qquick_window.setScreen(target_screen)
            w = qquick_window.width() if qquick_window.width() > 0 else 543
            h = qquick_window.height() if qquick_window.height() > 0 else 648
            x = geom.x() + (geom.width() - w) // 2
            y = geom.y() + max(40, (geom.height() - h) // 3)
            qquick_window.setPosition(x, y)

    @Slot(result="QVariantMap")
    def getInitialAudioInfo(self):
        vol_pct = 50
        is_muted = False
        if IS_WINDOWS and self._win_audio:
            vol_pct, is_muted = self._win_audio.get_volume_info()
            return {"volume": vol_pct, "muted": is_muted}
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
        if IS_WINDOWS and self._win_audio:
            self._win_audio.set_volume(volume)
            return
        vol = max(0, min(100, volume)) / 100.0
        try:
            subprocess.Popen(["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{vol:.2f}"])
        except Exception:
            pass

    @Slot()
    def toggleSystemMute(self):
        if IS_WINDOWS and self._win_audio:
            self._win_audio.toggle_mute()
            return
        try:
            subprocess.Popen(["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"])
        except Exception:
            pass

    @Slot(str, result=str)
    def listAudioDevices(self, kind="output"):
        if IS_WINDOWS and self._win_audio:
            return json.dumps(self._win_audio.list_devices(kind))
        return "[]"

    @Slot(str, str)
    def setDefaultAudioDevice(self, kind, dev_id):
        if IS_WINDOWS and self._win_audio:
            self._win_audio.set_default_device(kind, dev_id)

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
                "bgColor": v.get("bgColor", "#0b0c10"),
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

    @Slot(result="QVariantMap")
    def getAppAudioInfo(self):
        return {
            "volume": getattr(self, "_app_volume", 65),
            "muted": getattr(self, "_app_muted", False),
        }

    @Slot(int)
    def setAppVolume(self, vol):
        self._app_volume = max(0, min(100, int(vol)))
        self._apply_app_audio()

    @Slot()
    def toggleAppMute(self):
        self._app_muted = not getattr(self, "_app_muted", False)
        self._apply_app_audio()

    def _apply_app_audio(self):
        eff_vol = 0.0 if getattr(self, "_app_muted", False) else (getattr(self, "_app_volume", 65) / 100.0)
        if hasattr(self, "_snd_thock_pool"):
            for snd in self._snd_thock_pool:
                snd.setVolume(0.28 * eff_vol)
        if hasattr(self, "_snd_creamy"):
            self._snd_creamy.setVolume(0.32 * eff_vol)
        try:
            with open(self._user_sound_file, "w") as f:
                json.dump({"volume": self._app_volume, "muted": self._app_muted}, f)
        except Exception:
            pass
        self.appAudioChanged.emit(self._app_volume, self._app_muted)

    @Slot()
    def playThock(self):
        if getattr(self, "_app_muted", False) or getattr(self, "_app_volume", 65) <= 0:
            return
        try:
            if hasattr(self, "_snd_thock_pool") and self._snd_thock_pool:
                snd = self._snd_thock_pool[self._snd_thock_idx]
                self._snd_thock_idx = (self._snd_thock_idx + 1) % len(self._snd_thock_pool)
                snd.play()
            elif hasattr(self, "_snd_thock") and (self._snd_thock.status() == QSoundEffect.Ready or self._snd_thock.status() == QSoundEffect.Loading):
                self._snd_thock.play()
        except Exception:
            pass

    @Slot()
    def playCreamy(self):
        if getattr(self, "_app_muted", False) or getattr(self, "_app_volume", 65) <= 0:
            return
        try:
            if self._snd_creamy.status() == QSoundEffect.Ready or self._snd_creamy.status() == QSoundEffect.Loading:
                self._snd_creamy.play()
        except Exception:
            pass

    # ── KRunner System Actions ─────────────────────────────────────
    @Slot()
    def powerOff(self):
        """Sistemi kapat."""
        if IS_WINDOWS:
            subprocess.Popen(["shutdown.exe", "/s", "/t", "0"], **(wp._hidden_kwargs() if wp else {}))
        else:
            subprocess.Popen(
                ["bash", "-c", "qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logoutAndShutdown 2>/dev/null || qdbus6 org.kde.Shutdown /Shutdown logoutAndShutdown 2>/dev/null || systemctl poweroff"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def rebootSystem(self):
        """Sistemi yeniden başlat."""
        if IS_WINDOWS:
            subprocess.Popen(["shutdown.exe", "/r", "/t", "0"], **(wp._hidden_kwargs() if wp else {}))
        else:
            subprocess.Popen(
                ["bash", "-c", "qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logoutAndReboot 2>/dev/null || qdbus6 org.kde.Shutdown /Shutdown logoutAndReboot 2>/dev/null || systemctl reboot"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def rebootToBios(self):
        """Sistemi doğrudan UEFI / BIOS menüsüne yeniden başlat."""
        if IS_WINDOWS and wp:
            ok, err = wp.reboot_to_bios()
            if not ok and err:
                self.notify("Reboot to BIOS Failed", err)
        else:
            subprocess.Popen(
                ["systemctl", "reboot", "--firmware-setup"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def lockScreen(self):
        """Ekranı kilitle."""
        if IS_WINDOWS and wp:
            wp.lock_workstation()
        else:
            subprocess.Popen(
                ["bash", "-c", "loginctl lock-session 2>/dev/null || qdbus6 org.kde.kscreenlocker /Main org.kde.kscreenlocker.lock 2>/dev/null"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def suspendSystem(self):
        """Sistemi uyku moduna / askıya al (Hibernate değil gerçek Sleep)."""
        if IS_WINDOWS and wp:
            wp.suspend_sleep()
        else:
            subprocess.Popen(
                ["systemctl", "suspend"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def logoutUser(self):
        """Oturumu kapat."""
        if IS_WINDOWS:
            subprocess.Popen(["shutdown.exe", "/l"], **(wp._hidden_kwargs() if wp else {}))
        else:
            subprocess.Popen(
                ["bash", "-c", "qdbus6 org.kde.Shutdown /Shutdown logout 2>/dev/null || loginctl terminate-user $USER"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot()
    def restartPlasma(self):
        """KDE Plasma kabuğunu yeniden başlat."""
        if not IS_WINDOWS:
            subprocess.Popen(
                ["bash", "-c", "kquitapp6 plasmashell 2>/dev/null || killall plasmashell 2>/dev/null; sleep 0.4; kstart plasmashell >/dev/null 2>&1 &"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
            )

    @Slot(str, result=str)
    def listProcesses(self, query=""):
        """Çalışan süreçleri filtrele (kill komutu için)."""
        if not query or not query.strip():
            return "[]"
        query_lower = query.lower().strip()
        results = []
        try:
            import psutil
            rows = []
            for p in psutil.process_iter(["pid", "name", "cmdline", "memory_info"]):
                try:
                    info = p.info
                    pid = info.get("pid") or 0
                    if pid <= 4:
                        continue
                    name = info.get("name") or ""
                    cmd_parts = info.get("cmdline") or []
                    cmd = " ".join(cmd_parts)[:300] if isinstance(cmd_parts, list) else str(cmd_parts)
                    if query_lower not in name.lower() and query_lower not in cmd.lower():
                        continue
                    mi = info.get("memory_info")
                    rss_kb = int(mi.rss // 1024) if mi else 0
                    if rss_kb >= 1024 * 1024:
                        rss_txt = f"{rss_kb / 1048576.0:.1f} GB"
                    elif rss_kb >= 1024:
                        rss_txt = f"{rss_kb / 1024.0:.1f} MB"
                    else:
                        rss_txt = f"{rss_kb} KB"
                    rows.append((rss_kb, {
                        "pid": pid,
                        "name": name,
                        "cmd": cmd,
                        "rssKb": rss_kb,
                        "rssText": rss_txt,
                    }))
                except Exception:
                    continue
            rows.sort(key=lambda x: x[0], reverse=True)
            results = [r[1] for r in rows[:12]]
        except Exception:
            if IS_WINDOWS:
                try:
                    p = subprocess.run(
                        ["tasklist", "/FO", "CSV", "/NH"],
                        capture_output=True, text=True, encoding="oem", errors="replace",
                        timeout=2, **(wp._hidden_kwargs() if wp else {})
                    )
                    for line in p.stdout.splitlines():
                        parts = line.strip().split('","')
                        if len(parts) >= 2:
                            name = parts[0].strip('"')
                            pid = parts[1].strip('"')
                            if query_lower in name.lower():
                                results.append({"name": name, "pid": int(pid) if pid.isdigit() else pid, "cmd": name, "rssKb": 0, "rssText": ""})
                                if len(results) >= 12:
                                    break
                except Exception:
                    pass
        return json.dumps(results)

    @Slot(str, result=bool)
    def killProcess(self, pid):
        """PID'si verilen süreci nazikçe sonlandır (PID <= 4 asla)."""
        try:
            p = int(pid)
            if p <= 4 or p == os.getpid():
                return False
            if IS_WINDOWS:
                # Graceful close first (WM_CLOSE via taskkill without /F)
                r = subprocess.run(["taskkill", "/PID", str(p)], capture_output=True, check=False, **(wp._hidden_kwargs() if wp else {}))
                return r.returncode == 0
            else:
                os.kill(p, signal.SIGTERM)
                return True
        except Exception:
            return False

    @Slot(str, result=bool)
    def forceKillProcess(self, pid):
        """PID'si verilen süreci zorla sonlandır (ikinci Enter)."""
        try:
            p = int(pid)
            if p <= 4 or p == os.getpid():
                return False
            if IS_WINDOWS:
                subprocess.run(["taskkill", "/F", "/PID", str(p)], capture_output=True, check=False, **(wp._hidden_kwargs() if wp else {}))
            else:
                os.kill(p, signal.SIGKILL)
            return True
        except Exception:
            return False

    # ── Phase 2: Async File Search & Window Switcher ──────────────
    @Slot(str, int)
    def requestFileSearch(self, pattern, request_id):
        pat = (pattern or "").strip()
        rid = int(request_id)
        self._file_search_seq = rid
        def _worker():
            if rid != self._file_search_seq:
                return
            res = self.searchFiles(pat)
            if rid == self._file_search_seq:
                self.fileSearchFinished.emit(rid, pat, res)
        threading.Thread(target=_worker, daemon=True).start()

    @Slot(str, int)
    def requestWindowSearch(self, query, request_id):
        q = (query or "").strip()
        rid = int(request_id)
        def _worker():
            if IS_WINDOWS and wp:
                wins = wp.list_open_windows(q, exclude_pid=os.getpid())
                self.windowSearchFinished.emit(rid, q, json.dumps(wins))
            else:
                self.windowSearchFinished.emit(rid, q, "[]")
        threading.Thread(target=_worker, daemon=True).start()

    @Slot(str, result=bool)
    def activateWindow(self, match_id):
        if IS_WINDOWS and wp:
            return wp.activate_window_by_id(match_id)
        return False

    # ── Phase 2: Paste-to-Run Command Analyzer & Runner ───────────
    @Slot(str, result="QVariantMap")
    def analyzeCommand(self, text):
        raw = (text or "").strip()
        if not raw:
            return {"signal": 0, "dangerous": False, "needsTerminal": False, "firstLine": "", "extraLines": 0}
        lines = [ln.strip() for ln in raw.splitlines() if ln.strip()]
        first_ln = lines[0] if lines else ""
        extra = max(0, len(lines) - 1)
        tokens = first_ln.split()
        tok0 = tokens[0].lower() if tokens else ""

        strong_re = re.compile(r"(\||&&|;|\$\(|`|\n|(^|\s)\d?[<>]{1,2}\s*\S)")
        priv_re = re.compile(r"^(sudo|doas|pkexec|runas)(\s|$)", re.I)
        sig = 0
        if priv_re.search(raw) or strong_re.search(raw):
            sig = 2
        elif raw.startswith(("./", "~/", "/")) or (IS_WINDOWS and re.match(r"^[A-Za-z]:[\\/]", raw)):
            sig = 2 if len(tokens) >= 2 else 1
        elif len(tokens) >= 2 and (self.isCommandAvailable(tok0) or tok0 in (
            "dir", "cd", "echo", "cls", "type", "copy", "move", "del", "rd", "md", "mkdir",
            "ipconfig", "ping", "tracert", "netstat", "nslookup", "tasklist", "taskkill",
            "winget", "choco", "scoop", "git", "python", "pip", "node", "npm", "curl", "ssh",
            "scp", "powercfg", "sfc", "dism", "wsl", "get-process", "get-service", "get-childitem",
        ) or "-" in tok0 and tok0.startswith(("get-", "set-", "new-", "remove-", "start-", "stop-", "restart-", "test-"))):
            sig = 1

        dangerous = bool(re.search(
            r"(rm\s+-[a-z]*r[a-z]*\s+(/|\*|~)|del\s+/[sqf]+|rd\s+/s|rmdir\s+/s|remove-item\s+.*-recurse|format\s+[a-z]:|diskpart|bcdedit\s+/delete)",
            raw, re.I
        ))
        interactive_cmds = {"ssh", "vim", "nvim", "vi", "nano", "btop", "htop", "top", "less", "more", "diskpart", "wsl", "python", "node", "powershell", "pwsh", "cmd"}
        needs_term = bool(priv_re.search(raw) or tok0 in interactive_cmds)
        return {
            "signal": sig,
            "dangerous": dangerous,
            "needsTerminal": needs_term,
            "firstLine": first_ln,
            "extraLines": extra,
        }

    def _record_cmd_history(self, cmd):
        c = (cmd or "").strip()
        if not c:
            return
        self._cmd_history = [x for x in self._cmd_history if x != c]
        self._cmd_history.insert(0, c)
        self._cmd_history = self._cmd_history[:60]
        self._save_json_list(self._cmd_hist_file, self._cmd_history)

    @Slot(str)
    def runCommandInTerminal(self, command):
        c = (command or "").strip()
        if not c:
            return
        self._record_cmd_history(c)
        self.runInTerminal(c)

    @Slot(str, result=bool)
    def runCommandInline(self, command):
        c = (command or "").strip()
        if not c:
            return False
        info = self.analyzeCommand(c)
        if info.get("needsTerminal"):
            self.runCommandInTerminal(c)
            return False
        if self._runner.running:
            return False
        self._record_cmd_history(c)
        return self._runner.start(c)

    @Slot()
    def stopInlineCommand(self):
        self._runner.stop()

    @Slot(str, result="QStringList")
    def commandHistory(self, query=""):
        ql = (query or "").strip().lower()
        if not ql:
            return self._cmd_history[:12]
        return [x for x in self._cmd_history if ql in x.lower()][:12]

    # ── Phase 3: File & App Context Actions ───────────────────────
    @Slot(str, result="QVariantMap")
    def fileProperties(self, path):
        if IS_WINDOWS and wp:
            return wp.get_file_properties(path)
        return {"name": os.path.basename(path or ""), "path": path or "", "isDir": os.path.isdir(path or ""),
                "sizeText": "--", "mime": "", "mimeComment": "", "modified": "--", "permissions": "", "owner": ""}

    @Slot(str, str, result=str)
    def renamePath(self, path, new_name):
        if IS_WINDOWS and wp:
            return wp.rename_file_path(path, new_name)
        return "Not supported"

    @Slot(str, result=bool)
    def isArchive(self, path):
        if IS_WINDOWS and wp:
            return wp.is_archive_path(path)
        return False

    @Slot(str)
    def extractHere(self, path):
        if IS_WINDOWS and wp:
            wp.extract_archive_here(path)
            self.notify("Extracting Archive", os.path.basename(path or ""))

    @Slot(str)
    def compressPath(self, path):
        if IS_WINDOWS and wp:
            wp.compress_to_zip(path)
            self.notify("Compressing to ZIP", os.path.basename(path or ""))

    @Slot(str, result=str)
    def openWithApps(self, path):
        if IS_WINDOWS and wp:
            return json.dumps(wp.list_open_with_apps(path))
        return "[]"

    @Slot(str, str)
    def openWith(self, path, app_path):
        if IS_WINDOWS and wp:
            wp.open_path_with(path, app_path)

    @Slot(str)
    def copyFileToClipboard(self, path):
        if not path:
            return
        try:
            exp = os.path.abspath(os.path.expandvars(os.path.expanduser(path)))
            md = QMimeData()
            md.setUrls([QUrl.fromLocalFile(exp)])
            cb = QApplication.clipboard()
            if cb:
                cb.setMimeData(md)
        except Exception:
            pass

    @Slot(str, result=bool)
    def isAutostart(self, desktop_file):
        if not desktop_file:
            return False
        if IS_WINDOWS:
            startup = os.path.expandvars(r"%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup")
            return os.path.isfile(os.path.join(startup, os.path.basename(desktop_file)))
        return False

    @Slot(str, bool, result=bool)
    def setAutostart(self, desktop_file, enabled):
        if not desktop_file or not IS_WINDOWS:
            return False
        startup = os.path.expandvars(r"%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup")
        target = os.path.join(startup, os.path.basename(desktop_file))
        try:
            if not enabled:
                if os.path.exists(target):
                    os.remove(target)
                return True
            if os.path.isfile(desktop_file):
                os.makedirs(startup, exist_ok=True)
                shutil.copy2(desktop_file, target)
                return True
        except Exception:
            pass
        return False

    @Slot(str, result=bool)
    def canUninstall(self, desktop_file):
        return bool(IS_WINDOWS and desktop_file)

    @Slot(str)
    def uninstallApp(self, desktop_file):
        if IS_WINDOWS:
            self.runDetached("ms-settings:appsfeatures")

    # ── Phase 4: Recent Files, Emoji, Color Picker & Misc ─────────
    @Slot(str, result=str)
    def recentFiles(self, query=""):
        if IS_WINDOWS and wp:
            return json.dumps(wp.list_windows_recent_files(query))
        return "[]"

    def _ensure_emoji(self):
        if self._emoji_entries is not None:
            return
        self._emoji_entries = []
        path = os.path.join(APP_DIR, "assets", "emoji.json")
        if not os.path.isfile(path):
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                arr = json.load(f)
            for e in arr:
                if not isinstance(e, list) or len(e) < 4:
                    continue
                glyph = str(e[0])
                name = str(e[1])
                aliases = [a for a in str(e[2]).lower().split() if a]
                words = [w for w in re.split(r"[\s_-]+", (name + " " + str(e[3])).lower()) if w]
                if glyph:
                    self._emoji_entries.append((glyph, name, aliases, words))
        except Exception:
            pass

    @Slot(str, result=str)
    def searchEmoji(self, query=""):
        self._ensure_emoji()
        q = (query or "").strip().lower()
        if q.startswith(":"):
            q = q[1:]
        hits = []
        for idx, (glyph, name, aliases, words) in enumerate(self._emoji_entries):
            score = -1
            if not q:
                score = 3
            elif q in aliases:
                score = 0
            elif any(a.startswith(q) for a in aliases):
                score = 1
            elif any(w.startswith(q) for w in words):
                score = 2
            if score >= 0:
                hits.append((score, idx, glyph, name))
        hits.sort(key=lambda x: (x[0], x[1]))
        out = [{"glyph": g, "name": n} for _, _, g, n in hits[:24]]
        return json.dumps(out)

    @Slot()
    def pickColor(self):
        if not IS_WINDOWS or not wp:
            return
        def _start():
            def _on_picked(hex_code):
                self.setClipboard(hex_code)
                self.notify("Color Picked", f"Copied {hex_code} to clipboard")
            def _on_closed():
                self._color_picker_win = None
            self._color_picker_win = wp.launch_color_picker(_on_picked, on_closed=_on_closed)
        QTimer.singleShot(180, _start)

    @Slot(result=str)
    def detectGpuVendor(self):
        if IS_WINDOWS:
            if self._win_lhm and self._win_lhm.data.get("gpu_name"):
                gn = self._win_lhm.data["gpu_name"].lower()
                if "amd" in gn or "radeon" in gn:
                    return "amd"
                if "nvidia" in gn or "geforce" in gn or "rtx" in gn:
                    return "nvidia"
                if "intel" in gn or "arc" in gn:
                    return "intel"
            return "windows"
        return "unknown"

    @Slot(str, result=bool)
    def isCommandAvailable(self, command):
        if not command:
            return False
        return shutil.which(command) is not None

    @Slot(result=str)
    def getNashiPath(self):
        for cand in (
            shutil.which("nashi") or "",
            os.path.expandvars(r"%LOCALAPPDATA%\nashi\nashi.exe"),
        ):
            if cand and os.path.isfile(cand):
                return cand
        return ""

    @Slot(str, str)
    def installPackage(self, pkg_name, custom_cmd):
        if custom_cmd == "install-nashi":
            self.openPath("https://github.com/cdokuro/nashi-app/releases")
            return
        if custom_cmd:
            self.runCommandInTerminal(custom_cmd)

    @Slot()
    def saveSession(self):
        self.notify("RavenSpot", "Session state saved.")


def _fallback_icon_image(sz: QSize) -> QImage:
    w = max(16, sz.width() if sz.isValid() and sz.width() > 0 else 48)
    h = max(16, sz.height() if sz.isValid() and sz.height() > 0 else 48)
    img = QImage(w, h, QImage.Format_ARGB32)
    img.fill(0)
    return img


class AppIconProvider(QQuickImageProvider):
    """QML'e `image://appicon/<desktop-id>` üzerinden uygulama ikonu verir."""

    def __init__(self, shell, candy_resolver=None):
        super().__init__(QQuickImageProvider.Image)
        self._shell = shell
        self._candy = candy_resolver
        self._file_icon_provider = QFileIconProvider()

    def requestImage(self, id, size, requestedSize):
        from urllib.parse import unquote
        clean_id = unquote(id.split("?")[0] if "?" in id else id)
        sz = requestedSize if requestedSize.isValid() and requestedSize.width() > 0 else QSize(48, 48)

        target_path = clean_id
        app_label = ""
        if not os.path.exists(target_path):
            for app in self._shell._load_apps():
                if app["id"] == clean_id:
                    target_path = app.get("desktop") or app.get("icon") or clean_id
                    app_label = app.get("label", "")
                    break

        if IS_WINDOWS and target_path and os.path.exists(target_path):
            try:
                from PySide6.QtCore import QFileInfo
                fi_icon = self._file_icon_provider.icon(QFileInfo(target_path))
                if not fi_icon.isNull():
                    pm = fi_icon.pixmap(sz)
                    img = pm.toImage()
                    if not img.isNull():
                        size.setWidth(img.width())
                        size.setHeight(img.height())
                        return img
            except Exception:
                pass

        # Fallback: try active icon theme first, then candy-icons
        active_theme = self._shell.currentTheme().get("iconTheme", "candy-icons") if self._shell else "candy-icons"
        icon = QIcon()
        if self._candy:
            for cand in (clean_id.replace("uwp-", ""), app_label.lower().replace(" ", "-"), "application-x-executable"):
                svg = self._candy.resolve(cand, theme_name=active_theme)
                if svg:
                    icon = QIcon(svg)
                    if not icon.isNull():
                        break
        if icon.isNull():
            icon = QIcon.fromTheme(clean_id)
        if icon.isNull():
            icon = QIcon.fromTheme("application-x-executable")
        pm = icon.pixmap(sz)
        img = pm.toImage() if not pm.isNull() else _fallback_icon_image(sz)
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img


class ThemeIconProvider(QQuickImageProvider):
    """QML'e `image://themeicon/<freedesktop-ikon-adi>` üzerinden aktif temanın ikonunu verir."""

    def __init__(self, candy_resolver=None, shell=None):
        super().__init__(QQuickImageProvider.Image)
        self._candy = candy_resolver
        self._shell = shell
        self._file_icon_provider = QFileIconProvider() if IS_WINDOWS else None

    def _active_icon_theme(self):
        if self._shell is not None:
            try:
                return self._shell.currentTheme().get("iconTheme", "candy-icons")
            except Exception:
                pass
        return "candy-icons"

    def requestImage(self, id, size, requestedSize):
        from urllib.parse import unquote
        clean_id = unquote(id.split("?")[0] if "?" in id else id)
        icon = QIcon()
        if IS_WINDOWS and clean_id and os.path.exists(clean_id):
            if clean_id.lower().endswith((".exe", ".lnk", ".ico")):
                try:
                    from PySide6.QtCore import QFileInfo
                    icon = self._file_icon_provider.icon(QFileInfo(clean_id))
                except Exception:
                    pass
            if icon.isNull():
                icon = QIcon(clean_id)
        elif os.path.exists(clean_id) or os.path.isabs(clean_id):
            icon = QIcon(clean_id)

        active_theme = self._active_icon_theme()
        if icon.isNull() and self._candy:
            svg = self._candy.resolve(clean_id, theme_name=active_theme)
            if svg:
                icon = QIcon(svg)
        if icon.isNull() and clean_id in ("folder-download", "folder-downloads", "download"):
            icon = QIcon.fromTheme("aptdaemon-download")
        if icon.isNull():
            icon = QIcon.fromTheme(clean_id)
        if icon.isNull() and self._candy:
            svg = self._candy.resolve("application-x-executable", theme_name=active_theme)
            if svg:
                icon = QIcon(svg)
        if icon.isNull():
            icon = QIcon.fromTheme("application-x-executable")
        w = max(requestedSize.width() if requestedSize.isValid() and requestedSize.width() > 0 else 48, 64)
        h = max(requestedSize.height() if requestedSize.isValid() and requestedSize.height() > 0 else 48, 64)
        pm = icon.pixmap(QSize(w, h))
        img = pm.toImage() if not pm.isNull() else _fallback_icon_image(QSize(w, h))
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img


def main():
    start_hidden = START_HIDDEN

    # Single-instance guard on Windows via named mutex (prevents duplicate daemons/trays)
    _mutex_handle = None
    if IS_WINDOWS and wp and os.environ.get("RAVENSPOT_TESTING") != "1":
        _mutex_handle, already_running = wp.acquire_single_instance_mutex()
        if already_running:
            sys.exit(0)

    # ── Daemon Başlatma ──────────────────────────────────────────
    QApplication.setDesktopFileName("ravenspot")
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

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
    # Bundled Nerd Font (eliminates missing glyph boxes on Windows)
    loaded_icon_font = ""
    nf_path = os.path.join(APP_DIR, "assets", "SymbolsNerdFont-Regular.ttf")
    if os.path.isfile(nf_path):
        fid = QFontDatabase.addApplicationFont(nf_path)
        if fid >= 0:
            fams = QFontDatabase.applicationFontFamilies(fid)
            if fams:
                loaded_icon_font = fams[0]
    shell = Shell(start_hidden=start_hidden)
    shell._icon_font_name = loaded_icon_font
    candy_resolver = wp.CandyIconResolver(os.path.join(APP_DIR, "icons")) if (IS_WINDOWS and wp) else None

    # Live Windows clipboard monitoring
    if IS_WINDOWS and app.clipboard():
        app.clipboard().dataChanged.connect(shell._on_clipboard_changed)
        shell._on_clipboard_changed()

    def handle_new_connection():
        while server.hasPendingConnections():
            sock = server.nextPendingConnection()
            if not sock:
                continue
            sock.disconnected.connect(sock.deleteLater)
            def _make_reader(s):
                def _on_ready():
                    try:
                        raw = bytes(s.readAll())
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
                                shell._apps_cache = None
                                shell._win_uwp_cache = None
                                shell.reloadRequested.emit()
                            elif cmd_line == "quit":
                                QApplication.quit()
                    except Exception as e:
                        print(f"ravenspot IPC hata: {e}", flush=True)
                return _on_ready
            reader = _make_reader(sock)
            sock.readyRead.connect(reader)
            if sock.bytesAvailable() > 0:
                reader()

    server.newConnection.connect(handle_new_connection)

    hotkey_thread = None
    if IS_WINDOWS:
        hotkey_thread = Win32HotkeyThread()
        # Connect to QObject slot (queued cross-thread delivery to main GUI thread!)
        hotkey_thread.hotkeyTriggered.connect(shell.toggleMenu)
        hotkey_thread.hotkeyWarning.connect(lambda m: shell.notify("RavenSpot Hotkey", m))
        hotkey_thread.start()
        app.aboutToQuit.connect(hotkey_thread.stop)

    # Windows System Tray Icon
    tray = None
    if IS_WINDOWS and QSystemTrayIcon.isSystemTrayAvailable():
        tray_icon_path = candy_resolver.resolve("applications-all", theme_name=shell.currentTheme().get("iconTheme")) if candy_resolver else ""
        tray_icon = QIcon(tray_icon_path) if tray_icon_path else QIcon.fromTheme("applications-all")
        tray = QSystemTrayIcon(tray_icon, app)
        tray.setToolTip("RavenSpot — Press Alt+Space or Win+Space")
        tray_menu = QMenu()
        act_open = QAction("Open RavenSpot (Alt+Space)", tray_menu)
        act_open.triggered.connect(shell.toggleMenu)
        act_reload = QAction("Reload Menu & Apps", tray_menu)
        def _do_reload():
            shell._apps_cache = None
            shell._win_uwp_cache = None
            shell.reloadRequested.emit()
        act_reload.triggered.connect(_do_reload)
        act_quit = QAction("Quit RavenSpot", tray_menu)
        act_quit.triggered.connect(app.quit)
        tray_menu.addAction(act_open)
        tray_menu.addAction(act_reload)
        tray_menu.addSeparator()
        tray_menu.addAction(act_quit)
        tray.setContextMenu(tray_menu)
        tray.activated.connect(lambda reason: shell.toggleMenu() if reason == QSystemTrayIcon.Trigger else None)
        tray.show()
        shell.attachTray(tray)

    app.aboutToQuit.connect(shell.cleanup)

    engine = QQmlApplicationEngine()
    if not IS_WINDOWS:
        for p in ("/usr/lib/qt6/qml", "/usr/lib64/qt6/qml", "/usr/local/lib/qt6/qml"):
            if os.path.isdir(p) and p not in engine.importPathList():
                engine.addImportPath(p)
    engine.addImageProvider("appicon", AppIconProvider(shell, candy_resolver))
    engine.addImageProvider("themeicon", ThemeIconProvider(candy_resolver, shell))
    ctx = engine.rootContext()
    ctx.setContextProperty("Shell", shell)

    qml_path = os.path.join(APP_DIR, "Menu.qml")
    engine.load(QUrl.fromLocalFile(qml_path))

    if not engine.rootObjects():
        print("ravenspot: QML yüklenemedi", file=sys.stderr)
        server.close()
        if not IS_WINDOWS and os.path.exists(SOCKET_PATH):
            try:
                os.unlink(SOCKET_PATH)
            except OSError:
                pass
        sys.exit(1)

    shell.attachWindow(engine.rootObjects()[0])
    if IS_WINDOWS and not start_hidden:
        shell.centerWindowOnActiveScreen(engine.rootObjects()[0])
        shell.onMenuVisibilityChanged(True)

    ret = app.exec()
    server.close()
    if not IS_WINDOWS and os.path.exists(SOCKET_PATH):
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
    sys.exit(ret)


if __name__ == "__main__":
    main()
