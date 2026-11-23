"""
RavenSpot — Native Windows 11 Platform Integration Module.

Provides:
  - Win32 window enumeration & activation (EnumWindows + DWMWA_CLOAKED filter)
  - Win32 foreground focus forcing (AttachThreadInput + SetForegroundWindow)
  - Win32 Core Audio volume / mute / device enumeration (via pycaw/comtypes if available, ctypes fallback)
  - Win32 Sleep Inhibit (SetThreadExecutionState)
  - Windows Power Plans (powercfg) & Dark/Light Mode (Personalize registry)
  - Windows Start Menu + UWP (shell:AppsFolder) app indexing
  - Windows Recent Files (.lnk target resolution)
  - LibreHardwareMonitor optional live GPU % and CPU temp reader (for AMD/Intel/NVIDIA)
  - Native Qt full-screen Pixel Color Picker overlay (magnifier + HEX copy)
  - Candy-icons text-symlink resolver (fixes git/ZIP symlinks on Windows)
"""

import ctypes
import datetime
import json
import mimetypes
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import zipfile
from ctypes import wintypes

IS_WINDOWS = sys.platform == "win32"


def fold_text(s: str) -> str:
    """Turkish-aware case folding (prevents U+0130 'İ' -> 'i\\u0307' mismatch)."""
    if not s:
        return ""
    return s.replace("\u0130", "i").replace("I", "i").lower().replace("\u0307", "")


def _hidden_kwargs():
    if not IS_WINDOWS:
        return {}
    return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)}


# ── Explicit Win32 ctypes Signatures (prevents 64-bit HANDLE/HWND & ULONGLONG truncation) ──
if IS_WINDOWS:
    _u32 = ctypes.windll.user32
    _k32 = ctypes.windll.kernel32
    _s32 = ctypes.windll.shell32

    _u32.GetForegroundWindow.restype = wintypes.HWND
    _u32.IsIconic.argtypes = [wintypes.HWND]
    _u32.IsIconic.restype = wintypes.BOOL
    _u32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    _u32.ShowWindow.restype = wintypes.BOOL
    _u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    _u32.GetWindowThreadProcessId.restype = wintypes.DWORD
    _u32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
    _u32.AttachThreadInput.restype = wintypes.BOOL
    _u32.BringWindowToTop.argtypes = [wintypes.HWND]
    _u32.BringWindowToTop.restype = wintypes.BOOL
    _u32.SetForegroundWindow.argtypes = [wintypes.HWND]
    _u32.SetForegroundWindow.restype = wintypes.BOOL
    _u32.SetActiveWindow.argtypes = [wintypes.HWND]
    _u32.SetActiveWindow.restype = wintypes.HWND
    _u32.IsWindowVisible.argtypes = [wintypes.HWND]
    _u32.IsWindowVisible.restype = wintypes.BOOL
    _u32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
    _u32.GetWindowLongW.restype = wintypes.LONG
    _u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    _u32.GetWindowTextLengthW.restype = ctypes.c_int
    _u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    _u32.GetWindowTextW.restype = ctypes.c_int
    _u32.LockWorkStation.argtypes = []
    _u32.LockWorkStation.restype = wintypes.BOOL

    _k32.GetCurrentThreadId.argtypes = []
    _k32.GetCurrentThreadId.restype = wintypes.DWORD
    _k32.GetTickCount64.argtypes = []
    _k32.GetTickCount64.restype = ctypes.c_uint64
    _k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _k32.OpenProcess.restype = wintypes.HANDLE
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _k32.CloseHandle.restype = wintypes.BOOL
    _k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    _k32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    _k32.SetThreadExecutionState.argtypes = [wintypes.DWORD]
    _k32.SetThreadExecutionState.restype = wintypes.DWORD
    _k32.CreateMutexW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR]
    _k32.CreateMutexW.restype = wintypes.HANDLE
    _k32.GetLastError.argtypes = []
    _k32.GetLastError.restype = wintypes.DWORD

    _s32.ShellExecuteW.argtypes = [wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_int]
    _s32.ShellExecuteW.restype = wintypes.HINSTANCE

    try:
        _powr = ctypes.windll.powrprof
        _powr.SetSuspendState.argtypes = [wintypes.BOOL, wintypes.BOOL, wintypes.BOOL]
        _powr.SetSuspendState.restype = wintypes.BOOL
    except Exception:
        _powr = None


def acquire_single_instance_mutex(name="Local\\RavenSpotSingleInstanceMutex"):
    """Return (mutex_handle, already_exists) using Win32 named mutex."""
    if not IS_WINDOWS:
        return None, False
    try:
        handle = ctypes.windll.kernel32.CreateMutexW(None, False, name)
        already = ctypes.windll.kernel32.GetLastError() == 183  # ERROR_ALREADY_EXISTS
        return handle, already
    except Exception:
        return None, False


def get_uptime_seconds() -> int:
    if not IS_WINDOWS:
        return 0
    try:
        return int(ctypes.windll.kernel32.GetTickCount64() // 1000)
    except Exception:
        return 0


def lock_workstation() -> bool:
    if not IS_WINDOWS:
        return False
    try:
        return bool(ctypes.windll.user32.LockWorkStation())
    except Exception:
        return False


def suspend_sleep() -> bool:
    """Put Windows to S3/Modern Standby sleep (Hibernate=False, Force=False, DisableWake=False)."""
    if not IS_WINDOWS:
        return False
    try:
        return bool(ctypes.windll.powrprof.SetSuspendState(False, False, False))
    except Exception:
        return False


def reboot_to_bios():
    """Restart directly into UEFI / BIOS firmware setup. Returns (ok, error_message)."""
    if not IS_WINDOWS:
        return False, "Not Windows"
    try:
        is_admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        is_admin = False
    if is_admin:
        try:
            r = subprocess.run(
                ["shutdown.exe", "/r", "/fw", "/t", "0"],
                capture_output=True, text=True, encoding="oem", errors="replace",
                timeout=5, **_hidden_kwargs()
            )
            if r.returncode == 0:
                return True, ""
            return False, (r.stderr or r.stdout or f"shutdown /r /fw failed ({r.returncode})").strip()
        except Exception as e:
            return False, str(e)
    try:
        ret = ctypes.windll.shell32.ShellExecuteW(None, "runas", "shutdown.exe", "/r /fw /t 0", None, 0)
        if int(ret or 0) > 32:
            return True, ""
        return False, "UAC elevation cancelled or denied."
    except Exception as e:
        return False, str(e)


def _attach_kill_on_close_job(pid: int):
    """Attach a child process PID to a Win32 Job Object with KILL_ON_JOB_CLOSE so it never orphans."""
    if not IS_WINDOWS or not pid:
        return None
    try:
        k32 = ctypes.windll.kernel32
        k32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
        k32.CreateJobObjectW.restype = wintypes.HANDLE
        k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        k32.AssignProcessToJobObject.restype = wintypes.BOOL
        k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
        k32.SetInformationJobObject.restype = wintypes.BOOL

        class IO_COUNTERS(ctypes.Structure):
            _fields_ = [(n, ctypes.c_uint64) for n in ("ReadOperationCount","WriteOperationCount","OtherOperationCount","ReadTransferCount","WriteTransferCount","OtherTransferCount")]

        class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("PerProcessUserTimeLimit", ctypes.c_int64),
                ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD),
                ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t),
                ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t),
                ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD),
            ]

        class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION),
                ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t),
            ]

        job = k32.CreateJobObjectW(None, None)
        if not job:
            return None
        info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        info.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
        PROCESS_SET_QUOTA = 0x0100
        PROCESS_TERMINATE = 0x0001
        hproc = k32.OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, False, pid)
        if hproc:
            k32.AssignProcessToJobObject(job, hproc)
            k32.CloseHandle(hproc)
        return job
    except Exception:
        return None


# ── 1. Win32 Window Focus, Enumeration & Activation ──────────────────────────
def force_foreground_window(hwnd):
    """Reliably bring a window to foreground and give it keyboard focus on Windows 11."""
    if not IS_WINDOWS or not hwnd:
        return False
    try:
        user32 = ctypes.windll.user32
        kernel32 = ctypes.windll.kernel32
        hwnd = int(hwnd)
        SW_RESTORE = 9
        if user32.IsIconic(hwnd):
            user32.ShowWindow(hwnd, SW_RESTORE)
        fg = user32.GetForegroundWindow()
        cur_tid = kernel32.GetCurrentThreadId()
        fg_tid = user32.GetWindowThreadProcessId(fg, None) if fg else 0
        target_tid = user32.GetWindowThreadProcessId(hwnd, None)
        attached_fg = False
        attached_tgt = False
        if fg_tid and fg_tid != cur_tid:
            attached_fg = bool(user32.AttachThreadInput(cur_tid, fg_tid, True))
        if target_tid and target_tid != cur_tid:
            attached_tgt = bool(user32.AttachThreadInput(cur_tid, target_tid, True))
        try:
            user32.BringWindowToTop(hwnd)
            user32.SetForegroundWindow(hwnd)
            user32.SetActiveWindow(hwnd)
        finally:
            if attached_tgt:
                user32.AttachThreadInput(cur_tid, target_tid, False)
            if attached_fg:
                user32.AttachThreadInput(cur_tid, fg_tid, False)
        return True
    except Exception:
        return False


def list_open_windows(query="", exclude_pid=None):
    """Enumerate visible, non-cloaked top-level Windows matching query."""
    if not IS_WINDOWS:
        return []
    query_l = (query or "").strip().lower()
    if exclude_pid is None:
        exclude_pid = os.getpid()

    user32 = ctypes.windll.user32
    dwmapi = getattr(ctypes.windll, "dwmapi", None)
    kernel32 = ctypes.windll.kernel32

    GWL_EXSTYLE = -20
    WS_EX_TOOLWINDOW = 0x00000080
    DWMWA_CLOAKED = 14
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

    results = []
    EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def _get_exe_path(pid):
        try:
            h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
            if not h:
                return ""
            try:
                buf = ctypes.create_unicode_buffer(512)
                sz = wintypes.DWORD(512)
                if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(sz)):
                    return buf.value
            finally:
                kernel32.CloseHandle(h)
        except Exception:
            pass
        return ""

    def callback(hwnd, lparam):
        try:
            if not user32.IsWindowVisible(hwnd):
                return True
            ex = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
            if ex & WS_EX_TOOLWINDOW:
                return True
            if dwmapi:
                cloaked = ctypes.c_int(0)
                if dwmapi.DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, ctypes.byref(cloaked), ctypes.sizeof(cloaked)) == 0:
                    if cloaked.value != 0:
                        return True
            length = user32.GetWindowTextLengthW(hwnd)
            if length <= 0:
                return True
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            title = buf.value.strip()
            if not title or title in ("Program Manager", "Windows Input Experience", "Default IME"):
                return True
            pid = wintypes.DWORD(0)
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == exclude_pid:
                return True
            exe_path = _get_exe_path(pid.value)
            exe_base = os.path.splitext(os.path.basename(exe_path))[0] if exe_path else ""
            if query_l:
                if query_l not in title.lower() and query_l not in exe_base.lower():
                    return True
            results.append({
                "id": str(hwnd),
                "title": title,
                "iconName": exe_path if exe_path else "window",
            })
        except Exception:
            pass
        return True

    try:
        user32.EnumWindows(EnumWindowsProc(callback), 0)
    except Exception:
        pass
    return results[:12]


def activate_window_by_id(match_id):
    try:
        hwnd = int(str(match_id).strip())
        return force_foreground_window(hwnd)
    except Exception:
        return False


# ── 2. Sleep Inhibit (SetThreadExecutionState) ───────────────────────────────
def set_sleep_inhibited(inhibit: bool) -> bool:
    if not IS_WINDOWS:
        return False
    ES_CONTINUOUS = 0x80000000
    ES_SYSTEM_REQUIRED = 0x00000001
    ES_DISPLAY_REQUIRED = 0x00000002
    try:
        flags = (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) if inhibit else ES_CONTINUOUS
        res = ctypes.windll.kernel32.SetThreadExecutionState(flags)
        return res != 0
    except Exception:
        return False


# ── 3. Windows Power Profiles (powercfg) ─────────────────────────────────────
WIN_POWER_GUIDS = {
    "power-saver": "a1841308-3541-4fab-bc81-f71556f20b4a",
    "balanced": "381b4222-f694-41f0-9685-ff5bb260df2e",
    "performance": "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c",
}


def _scan_power_schemes():
    """Return (active_guid, [(guid, name), ...])."""
    schemes = []
    active_guid = ""
    try:
        p = subprocess.run(
            ["powercfg", "/list"],
            capture_output=True, text=True, encoding="oem", errors="replace",
            timeout=3, **_hidden_kwargs()
        )
        for line in p.stdout.splitlines():
            m = re.search(r"([0-9a-fA-F-]{36})\s*\(([^)]+)\)\s*(\*?)", line)
            if m:
                g = m.group(1).lower()
                name = m.group(2).strip()
                is_act = "*" in m.group(3) or line.rstrip().endswith("*")
                schemes.append((g, name))
                if is_act:
                    active_guid = g
    except Exception:
        pass
    return active_guid, schemes


def get_windows_power_profile() -> str:
    active_guid, schemes = _scan_power_schemes()
    for key, g in WIN_POWER_GUIDS.items():
        if active_guid == g:
            return key
    for g, name in schemes:
        if g == active_guid:
            nl = fold_text(name)
            if "sav" in nl or "tasarruf" in nl:
                return "power-saver"
            if any(k in nl for k in ("high", "ultimate", "yuksek", "yüksek", "nihai", "perf")):
                return "performance"
    return "balanced"


def list_windows_power_profiles():
    active_guid, schemes = _scan_power_schemes()
    if not schemes:
        return [
            {"value": "performance", "label": "High Performance", "current": False},
            {"value": "balanced", "label": "Balanced", "current": True},
            {"value": "power-saver", "label": "Power Saver", "current": False},
        ]
    rows = []
    for g, name in schemes:
        val = g
        for k, kg in WIN_POWER_GUIDS.items():
            if g == kg:
                val = k
                break
        rows.append({"value": val, "label": name, "current": g == active_guid})
    return rows


def set_windows_power_profile(profile: str) -> bool:
    if not profile:
        return False
    guid = WIN_POWER_GUIDS.get(profile.lower(), profile)
    # If GUID isn't present in powercfg /list, try creating/duplicating standard scheme or fallback
    active_guid, schemes = _scan_power_schemes()
    known = {g for g, _ in schemes}
    if guid.lower() not in known:
        # Match by friendly name
        for g, name in schemes:
            nl = fold_text(name)
            if profile == "performance" and any(k in nl for k in ("high", "ultimate", "yuksek", "yüksek", "nihai", "perf")):
                guid = g
                break
            if profile == "power-saver" and any(k in nl for k in ("sav", "tasarruf")):
                guid = g
                break
            if profile == "balanced" and any(k in nl for k in ("balanc", "dengeli")):
                guid = g
                break
    try:
        r = subprocess.run(
            ["powercfg", "/setactive", guid],
            capture_output=True, text=True, encoding="oem", errors="replace",
            timeout=3, **_hidden_kwargs()
        )
        return r.returncode == 0
    except Exception:
        return False


# ── 4. Windows Dark / Light Color Mode ───────────────────────────────────────
def list_windows_color_modes():
    if not IS_WINDOWS:
        return []
    import winreg
    apps_light = 0
    sys_light = 0
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as k:
            apps_light = winreg.QueryValueEx(k, "AppsUseLightTheme")[0]
            sys_light = winreg.QueryValueEx(k, "SystemUsesLightTheme")[0]
    except Exception:
        pass
    if apps_light == 0 and sys_light == 0:
        cur = "win-dark"
    elif apps_light == 1 and sys_light == 1:
        cur = "win-light"
    elif apps_light == 0 and sys_light == 1:
        cur = "win-dark-apps"
    else:
        cur = "win-dark-sys"
    modes = [
        ("win-dark", "Windows Dark Mode (System + Apps)"),
        ("win-light", "Windows Light Mode (System + Apps)"),
        ("win-dark-apps", "Dark Apps / Light Taskbar"),
        ("win-dark-sys", "Dark Taskbar / Light Apps"),
    ]
    rows = []
    for val, lbl in modes:
        rows.append({
            "value": val,
            "label": lbl,
            "current": val == cur,
            "isFavorite": False,
            "section": "current" if val == cur else "other",
        })
    rows.sort(key=lambda r: 0 if r["current"] else 1)
    return rows


def apply_windows_color_mode(mode: str):
    if not IS_WINDOWS or not mode:
        return
    import winreg
    mapping = {
        "win-dark": (0, 0),
        "win-light": (1, 1),
        "win-dark-apps": (0, 1),
        "win-dark-sys": (1, 0),
    }
    apps_v, sys_v = mapping.get(mode, (0, 0))
    try:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as k:
            winreg.SetValueEx(k, "AppsUseLightTheme", 0, winreg.REG_DWORD, apps_v)
            winreg.SetValueEx(k, "SystemUsesLightTheme", 0, winreg.REG_DWORD, sys_v)
        HWND_BROADCAST = 0xFFFF
        WM_SETTINGCHANGE = 0x001A
        SMTO_ABORTIFHUNG = 0x0002
        ctypes.windll.user32.SendMessageTimeoutW(
            HWND_BROADCAST, WM_SETTINGCHANGE, 0, "ImmersiveColorSet", SMTO_ABORTIFHUNG, 1000, None
        )
    except Exception:
        pass


# ── 5. Windows Core Audio (Volume, Mute, Devices) ────────────────────────────
class WinAudioController:
    """Fast Windows Core Audio controller using pycaw/comtypes if available."""

    def __init__(self):
        self._pycaw_ok = False
        if IS_WINDOWS:
            try:
                import pycaw  # noqa: F401
                import comtypes  # noqa: F401
                self._pycaw_ok = True
            except Exception:
                self._pycaw_ok = False

    def _with_com(self, fn):
        import comtypes
        co_inited = False
        try:
            comtypes.CoInitialize()
            co_inited = True
        except Exception:
            co_inited = False
        try:
            return fn()
        finally:
            if co_inited:
                try:
                    comtypes.CoUninitialize()
                except Exception:
                    pass

    def _get_endpoint(self):
        import comtypes
        from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
        speakers = AudioUtilities.GetSpeakers()
        if hasattr(speakers, "EndpointVolume"):
            return speakers.EndpointVolume
        iface = speakers.Activate(IAudioEndpointVolume._iid_, comtypes.CLSCTX_ALL, None)
        return ctypes.cast(iface, ctypes.POINTER(IAudioEndpointVolume))

    def get_volume_info(self):
        if self._pycaw_ok:
            try:
                def _read():
                    ep = self._get_endpoint()
                    vol = int(round(ep.GetMasterVolumeLevelScalar() * 100))
                    muted = bool(ep.GetMute())
                    return max(0, min(100, vol)), muted
                return self._with_com(_read)
            except Exception:
                pass
        return 50, False

    def set_volume(self, vol_pct: int):
        v = max(0, min(100, int(vol_pct))) / 100.0
        if self._pycaw_ok:
            try:
                def _write():
                    ep = self._get_endpoint()
                    ep.SetMasterVolumeLevelScalar(v, None)
                    if v > 0 and ep.GetMute():
                        ep.SetMute(0, None)
                    return True
                return self._with_com(_write)
            except Exception:
                pass
        return False

    def toggle_mute(self):
        if self._pycaw_ok:
            try:
                def _toggle():
                    ep = self._get_endpoint()
                    cur = bool(ep.GetMute())
                    ep.SetMute(0 if cur else 1, None)
                    return not cur
                return self._with_com(_toggle)
            except Exception:
                pass
        # Fallback: send VK_VOLUME_MUTE key
        try:
            VK_VOLUME_MUTE = 0xAD
            ctypes.windll.user32.keybd_event(VK_VOLUME_MUTE, 0, 0, 0)
            ctypes.windll.user32.keybd_event(VK_VOLUME_MUTE, 0, 2, 0)
        except Exception:
            pass
        return False

    def list_devices(self, kind="output"):
        if not self._pycaw_ok:
            return []
        try:
            def _list():
                from pycaw.pycaw import AudioUtilities, EDataFlow
                flow = EDataFlow.eRender.value if kind == "output" else EDataFlow.eCapture.value
                default_id = ""
                try:
                    enum = AudioUtilities.GetDeviceEnumerator()
                    def_dev = enum.GetDefaultAudioEndpoint(flow, 1)  # eMultimedia
                    default_id = def_dev.GetId()
                except Exception:
                    pass
                all_devs = AudioUtilities.GetAllDevices()
                out = []
                for d in all_devs:
                    try:
                        st_val = getattr(d.state, "value", d.state)
                        if st_val != 1:  # AudioDeviceState.Active == 1
                            continue
                        did = d.id or ""
                        if kind == "output" and not did.startswith("{0.0.0."):
                            continue
                        if kind == "input" and not did.startswith("{0.0.1."):
                            continue
                        name = d.FriendlyName or did
                        out.append({
                            "id": did,
                            "name": name,
                            "isDefault": did == default_id,
                        })
                    except Exception:
                        continue
                out.sort(key=lambda x: (0 if x["isDefault"] else 1, fold_text(x["name"])))
                return out
            return self._with_com(_list)
        except Exception:
            return []

    def set_default_device(self, kind, dev_id):
        if not IS_WINDOWS or not dev_id:
            return False
        try:
            def _apply():
                import comtypes
                from comtypes import GUID, COMMETHOD, IUnknown

                class IPolicyConfig(IUnknown):
                    _iid_ = GUID("{f8679f50-850a-41cf-9c72-430f290290c8}")
                    _methods_ = [
                        COMMETHOD([], ctypes.HRESULT, "GetMixFormat", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["out"], ctypes.POINTER(ctypes.c_void_p), "ppFormat")),
                        COMMETHOD([], ctypes.HRESULT, "GetDeviceFormat", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_int, "bDefault"), (["out"], ctypes.POINTER(ctypes.c_void_p), "ppFormat")),
                        COMMETHOD([], ctypes.HRESULT, "ResetDeviceFormat", (["in"], ctypes.c_wchar_p, "pwstrDeviceId")),
                        COMMETHOD([], ctypes.HRESULT, "SetDeviceFormat", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_void_p, "pEndpointFormat"), (["in"], ctypes.c_void_p, "pMixFormat")),
                        COMMETHOD([], ctypes.HRESULT, "GetProcessingPeriod", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_int, "bDefault"), (["out"], ctypes.c_void_p, "pmftDefaultPeriod"), (["out"], ctypes.c_void_p, "pmftMinimumPeriod")),
                        COMMETHOD([], ctypes.HRESULT, "SetProcessingPeriod", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_void_p, "pmftPeriod")),
                        COMMETHOD([], ctypes.HRESULT, "GetShareMode", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["out"], ctypes.c_void_p, "pMode")),
                        COMMETHOD([], ctypes.HRESULT, "SetShareMode", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_void_p, "pMode")),
                        COMMETHOD([], ctypes.HRESULT, "GetPropertyValue", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_void_p, "key"), (["out"], ctypes.c_void_p, "pv")),
                        COMMETHOD([], ctypes.HRESULT, "SetPropertyValue", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_void_p, "key"), (["in"], ctypes.c_void_p, "pv")),
                        COMMETHOD([], ctypes.HRESULT, "SetDefaultEndpoint", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_int, "role")),
                        COMMETHOD([], ctypes.HRESULT, "SetEndpointVisibility", (["in"], ctypes.c_wchar_p, "pwstrDeviceId"), (["in"], ctypes.c_int, "bVisible")),
                    ]

                CLSID_CPolicyConfigClient = GUID("{870af99c-171d-4f9e-af0d-e63df40c2bc9}")
                pc = comtypes.CoCreateInstance(CLSID_CPolicyConfigClient, interface=IPolicyConfig)
                for role in (0, 1, 2):  # Console, Multimedia, Communications
                    pc.SetDefaultEndpoint(dev_id, role)
                return True
            return self._with_com(_apply)
        except Exception:
            return False


# ── 6. LibreHardwareMonitor Live Telemetry Reader (GPU % + CPU Temp) ─────────
class WinLhmMiniPoller:
    """Optional background poller reading GPU load % and CPU temp from LibreHardwareMonitorLib.dll."""

    SCRIPT_TMPL = r"""
$ErrorActionPreference='SilentlyContinue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
Add-Type -Path '__DLL__'
$c = New-Object LibreHardwareMonitor.Hardware.Computer
$c.IsCpuEnabled = $true
$c.IsGpuEnabled = $true
$c.Open()
function Pick($sensors, $type, $names) {
    foreach ($n in $names) {
        foreach ($s in $sensors) {
            if ($s.SensorType.ToString() -eq $type -and $s.Name -eq $n -and $null -ne $s.Value) {
                return [double]$s.Value
            }
        }
    }
    return $null
}
while ($true) {
    $out = [ordered]@{}
    $bestGpu = $null
    $bestScore = -1
    foreach ($hw in $c.Hardware) {
        $hw.Update()
        $ht = $hw.HardwareType.ToString()
        if ($ht -eq 'Cpu') {
            $ct = Pick $hw.Sensors 'Temperature' @('Core (Tctl/Tdie)','CPU Package','CCD1 (Tdie)','Core Average')
            if ($null -ne $ct -and $ct -gt 1.0) { $out['cpu_temp'] = [int][math]::Round($ct) }
            $cw = Pick $hw.Sensors 'Power' @('Package','CPU Package')
            if ($null -ne $cw -and $cw -gt 0.1) { $out['cpu_watt'] = [int][math]::Round($cw) }
        } elseif ($ht -like 'Gpu*') {
            $vt = Pick $hw.Sensors 'SmallData' @('GPU Memory Total','D3D Dedicated Memory Total')
            $gp = Pick $hw.Sensors 'Power' @('GPU Package','GPU Core','GPU Total')
            $score = 0
            if ($null -ne $gp) { $score += 100000 }
            if ($null -ne $vt) { $score += [int]$vt }
            if ($hw.Sensors.Count -gt 0 -and $score -gt $bestScore) {
                $bestScore = $score
                $bestGpu = $hw
            }
        }
    }
    if ($null -ne $bestGpu) {
        $out['gpu_name'] = $bestGpu.Name
        $gl = Pick $bestGpu.Sensors 'Load' @('GPU Core','D3D 3D')
        if ($null -ne $gl) { $out['gpu_load'] = [int][math]::Round($gl) }
        $gt = Pick $bestGpu.Sensors 'Temperature' @('GPU Core','GPU Hot Spot')
        if ($null -ne $gt) { $out['gpu_temp'] = [int][math]::Round($gt) }
    }
    Write-Output ($out | ConvertTo-Json -Compress)
    [Console]::Out.Flush()
    Start-Sleep -Milliseconds 2000
}
"""

    def __init__(self, app_dir=""):
        self.data = {}
        self._proc = None
        self._job = None
        self._stop = False
        self.dll_path = self._find_dll(app_dir)
        self.available = bool(IS_WINDOWS and self.dll_path)
        if self.available:
            threading.Thread(target=self._run, daemon=True).start()

    @staticmethod
    def _find_dll(app_dir):
        local = os.environ.get("LOCALAPPDATA", "")
        cands = [
            os.path.join(app_dir, "lhm", "LibreHardwareMonitorLib.dll") if app_dir else "",
            os.path.join(local, "ravenspot", "lhm", "LibreHardwareMonitorLib.dll"),
            os.path.join(local, "oyun-izleyici", "lhm", "LibreHardwareMonitorLib.dll"),
        ]
        for p in cands:
            if p and os.path.isfile(p):
                return p
        return ""

    def _run(self):
        import base64
        script = self.SCRIPT_TMPL.replace("__DLL__", self.dll_path.replace("'", "''"))
        b64 = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
        while not self._stop:
            try:
                self._proc = subprocess.Popen(
                    ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                     "-EncodedCommand", b64],
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                    encoding="utf-8", errors="replace", **_hidden_kwargs()
                )
                self._job = _attach_kill_on_close_job(self._proc.pid)
                for line in self._proc.stdout:
                    if self._stop:
                        break
                    line = line.strip()
                    if line.startswith("{"):
                        try:
                            d = json.loads(line)
                            if isinstance(d, dict):
                                self.data = d
                        except Exception:
                            pass
            except Exception:
                pass
            if not self._stop:
                time.sleep(5.0)

    def stop(self):
        self._stop = True
        try:
            if self._proc:
                self._proc.terminate()
                try:
                    self._proc.wait(timeout=2)
                except Exception:
                    self._proc.kill()
        except Exception:
            pass
        if self._job:
            try:
                ctypes.windll.kernel32.CloseHandle(self._job)
            except Exception:
                pass
            self._job = None


# ── 7. Windows .lnk Target Resolution & Recent Files ─────────────────────────
def resolve_lnk_target(lnk_path):
    """Fast pure-Python Windows Shell Link (.lnk) target path extractor (with fallback)."""
    try:
        with open(lnk_path, "rb") as f:
            data = f.read(65536)
        if len(data) < 76 or data[:4] != b"\x4c\x00\x00\x00":
            return ""
        flags = struct.unpack_from("<I", data, 0x14)[0]
        pos = 0x4C
        # Skip LinkTargetIDList if present
        if flags & 0x01:
            if pos + 2 > len(data):
                return ""
            idlist_sz = struct.unpack_from("<H", data, pos)[0]
            pos += 2 + idlist_sz
        # Read LinkInfo if present
        if flags & 0x02 and pos + 28 <= len(data):
            info_sz = struct.unpack_from("<I", data, pos)[0]
            info_hdr_sz = struct.unpack_from("<I", data, pos + 4)[0]
            info_flags = struct.unpack_from("<I", data, pos + 8)[0]
            if info_flags & 0x01:  # VolumeIDAndLocalBasePath
                local_off = struct.unpack_from("<I", data, pos + 16)[0]
                if info_hdr_sz >= 0x24 and pos + 32 <= len(data):
                    u_off = struct.unpack_from("<I", data, pos + 28)[0]
                    if u_off and pos + u_off < len(data):
                        raw = data[pos + u_off : pos + info_sz]
                        end = len(raw)
                        for idx in range(0, len(raw) - 1, 2):
                            if raw[idx] == 0 and raw[idx + 1] == 0:
                                end = idx
                                break
                        if end > 0:
                            s = raw[:end].decode("utf-16-le", errors="ignore").rstrip("\x00")
                            if s and os.path.exists(s):
                                return s
                if local_off and pos + local_off < len(data):
                    raw = data[pos + local_off : pos + info_sz]
                    end = raw.find(b"\x00")
                    if end != -1:
                        s = raw[:end].decode("mbcs", errors="ignore")
                        if s and os.path.exists(s):
                            return s
    except Exception:
        pass
    return ""


def icon_name_for_path(path, is_dir=False):
    """Map a Windows file or folder path to a clean freedesktop/Fluent icon name."""
    if is_dir:
        return "folder"
    ext = os.path.splitext(path or "")[1].lower()
    if ext in (".png", ".jpg", ".jpeg", ".webp", ".svg", ".gif", ".ico", ".bmp", ".heic", ".avif"):
        return "image-x-generic"
    if ext in (".mp4", ".mkv", ".mov", ".avi", ".webm", ".flv", ".wmv", ".m4v"):
        return "video-x-generic"
    if ext in (".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".opus", ".wma"):
        return "audio-x-generic"
    if ext == ".pdf":
        return "application-pdf"
    if ext in (".zip", ".tar", ".gz", ".xz", ".7z", ".rar", ".bz2", ".zst", ".iso"):
        return "package-x-generic"
    if ext in (".doc", ".docx", ".odt", ".rtf"):
        return "application-msword"
    if ext in (".xls", ".xlsx", ".ods", ".csv"):
        return "application-vnd.ms-excel"
    if ext in (".ppt", ".pptx", ".odp"):
        return "application-vnd.ms-powerpoint"
    if ext == ".py":
        return "text-x-python"
    if ext in (".json", ".jsonc", ".yaml", ".yml", ".toml"):
        return "application-json"
    if ext in (".md", ".markdown"):
        return "text-markdown"
    if ext in (".ps1", ".bat", ".cmd", ".sh", ".bash"):
        return "application-x-shellscript"
    if ext in (".js", ".ts", ".tsx", ".jsx", ".html", ".css", ".cpp", ".c", ".h", ".rs", ".go", ".cs", ".java", ".qml", ".xml"):
        return "text-x-generic"
    if ext in (".exe", ".msi", ".msix", ".lnk"):
        return "application-x-executable"
    return "text-plain"


def list_windows_recent_files(query="", limit=30):
    if not IS_WINDOWS:
        return []
    recent_dir = os.path.join(os.environ.get("APPDATA", ""), "Microsoft", "Windows", "Recent")
    if not os.path.isdir(recent_dir):
        return []
    ql = (query or "").strip().lower()
    entries = []
    try:
        for e in os.scandir(recent_dir):
            if not e.name.lower().endswith(".lnk"):
                continue
            try:
                st = e.stat()
                entries.append((st.st_mtime, e.path, e.name[:-4]))
            except OSError:
                continue
    except OSError:
        return []
    entries.sort(key=lambda x: x[0], reverse=True)
    out = []
    seen = set()
    for mtime, lnk_path, fallback_name in entries:
        if len(out) >= limit:
            break
        target = resolve_lnk_target(lnk_path)
        if not target or not os.path.exists(target):
            continue
        norm = os.path.normpath(target)
        key = norm.lower()
        if key in seen:
            continue
        name = os.path.basename(norm.rstrip("\\/")) or fallback_name
        if ql and ql not in name.lower():
            continue
        seen.add(key)
        is_d = os.path.isdir(norm)
        dt = datetime.datetime.fromtimestamp(mtime).strftime("%d.%m.%Y %H:%M")
        out.append({
            "path": norm,
            "name": name,
            "isDir": is_d,
            "iconName": icon_name_for_path(norm, is_d),
            "modified": dt,
        })
    return out


# ── 8. UWP / Start Apps Indexing ─────────────────────────────────────────────
def scan_windows_uwp_apps():
    """Query Start Apps + AppxManifest logos via PowerShell so Store/UWP apps get crisp native Windows 11 icons."""
    if not IS_WINDOWS:
        return []
    apps = []
    try:
        ps = r"""
$ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$pkgs=@{}
Get-AppxPackage | ForEach-Object { if($_.PackageFamilyName -and $_.InstallLocation){ $pkgs[$_.PackageFamilyName]=$_.InstallLocation } }
$res = @()
Get-StartApps | Where-Object { $_.AppID -like '*!*' } | ForEach-Object {
  $pfn = ($_.AppID -split '!')[0]
  $aid = ($_.AppID -split '!')[1]
  $loc = $pkgs[$pfn]
  $logo = ''
  if ($loc -and (Test-Path "$loc\AppxManifest.xml")) {
    try {
      [xml]$xml = Get-Content "$loc\AppxManifest.xml" -Raw
      $appNodes = @($xml.Package.Applications.Application)
      $node = $appNodes | Where-Object { $_.Id -eq $aid } | Select-Object -First 1
      if (-not $node) { $node = $appNodes | Select-Object -First 1 }
      $rel = $node.VisualElements.Square44x44Logo
      if (-not $rel) { $rel = $node.VisualElements.Square150x150Logo }
      if (-not $rel) { $rel = $xml.Package.Properties.Logo }
      if ($rel) {
        $fullRel = Join-Path $loc $rel
        $dir = Split-Path $fullRel -Parent
        $base = [IO.Path]::GetFileNameWithoutExtension($rel)
        $ext = [IO.Path]::GetExtension($rel)
        if (Test-Path $dir) {
          $cands = @(Get-ChildItem $dir -Filter "$base*$ext" -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch 'contrast-white|contrast-black|lightunplated' })
          $unp = @($cands | Where-Object { $_.Name -match 'altform-unplated' } | Sort-Object Length -Descending)
          if ($unp.Count -gt 0) { $logo = $unp[0].FullName }
          elseif ($cands.Count -gt 0) { $logo = ($cands | Sort-Object Length -Descending | Select-Object -First 1).FullName }
        }
        if (-not $logo -and (Test-Path $fullRel)) { $logo = $fullRel }
      }
    } catch {}
  }
  $res += [pscustomobject]@{ Name = $_.Name; AppID = $_.AppID; Logo = $logo }
}
$res | ConvertTo-Json -Compress
"""
        import base64
        b64 = base64.b64encode(ps.encode("utf-16-le")).decode("ascii")
        p = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-EncodedCommand", b64],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=10, **_hidden_kwargs()
        )
        raw = (p.stdout or "").strip()
        if not raw:
            return []
        data = json.loads(raw)
        if isinstance(data, dict):
            data = [data]
        ignore = {"uninstall", "kaldır", "kaldir", "readme"}
        for item in data:
            name = (item.get("Name") or "").strip()
            appid = (item.get("AppID") or "").strip()
            logo = (item.get("Logo") or "").strip()
            if not name or not appid:
                continue
            nl = fold_text(name)
            if any(ig in nl for ig in ignore):
                continue
            if "!" not in appid:
                continue
            slug = re.sub(r"[^\w\s-]", "", nl).strip().replace(" ", "-")
            aliases = [w for w in re.split(r"[\s_-]+", nl) if len(w) > 1]
            cat = "other"
            if any(k in nl for k in ("xbox", "game", "solitaire", "oyun")):
                cat = "games"
            elif any(k in nl for k in ("edge", "mail", "outlook", "teams", "whatsapp", "telegram", "store")):
                cat = "internet"
            elif any(k in nl for k in ("terminal", "powershell", "wsl", "code", "dev")):
                cat = "development"
            elif any(k in nl for k in ("photo", "paint", "snip", "camera", "ekran", "foto")):
                cat = "graphics"
            elif any(k in nl for k in ("media", "music", "spotify", "sound", "clipchamp", "video", "netflix")):
                cat = "multimedia"
            elif any(k in nl for k in ("calc", "notepad", "not defteri", "clock", "saat", "weather", "hava")):
                cat = "utilities"
            elif any(k in nl for k in ("settings", "ayar", "security", "güvenlik", "defender", "store")):
                cat = "system"
            apps.append({
                "id": f"uwp-{slug or appid[:20]}",
                "label": name,
                "icon": logo if (logo and os.path.isfile(logo)) else "",
                "aliases": aliases,
                "desktop": f"shell:AppsFolder\\{appid}",
                "category": cat,
            })
    except Exception:
        pass
    return apps


# ── 9. Multi-Theme SVG Icon Resolver (Fluent Win11 + Candy Icons) ────────────
class CandyIconResolver:
    """Indexes icons/<theme>/ (fluent-win11, candy-icons, etc.) and resolves Linux symlink text files on Windows."""

    def __init__(self, icons_root):
        self._icons_root = icons_root or ""
        self._themes = {}  # theme_name -> { lower_stem: real_svg_path }
        self._index = {}   # default candy-icons index (backward compatible)
        if self._icons_root and os.path.isdir(self._icons_root):
            for theme_name in ("fluent-win11", "candy-icons"):
                tdir = os.path.join(self._icons_root, theme_name)
                if os.path.isdir(tdir):
                    self._themes[theme_name] = self._build_theme_index(tdir)
            self._index = self._themes.get("candy-icons", {})

    def _resolve_symlink_text(self, path, depth=0):
        if depth > 6 or not os.path.isfile(path):
            return path
        try:
            sz = os.path.getsize(path)
            if sz < 256:
                with open(path, "rb") as f:
                    head = f.read(256)
                if b"<svg" not in head.lower() and b"<?xml" not in head.lower():
                    target = head.decode("utf-8", errors="ignore").strip().splitlines()[0].strip()
                    if target and not target.startswith("<"):
                        cand = os.path.normpath(os.path.join(os.path.dirname(path), target))
                        if os.path.isfile(cand):
                            return self._resolve_symlink_text(cand, depth + 1)
        except Exception:
            pass
        return path

    def _build_theme_index(self, theme_dir):
        idx = {}
        prio_dirs = [
            os.path.join(theme_dir, "apps", "scalable"),
            os.path.join(theme_dir, "places", "48"),
            os.path.join(theme_dir, "devices", "scalable"),
            os.path.join(theme_dir, "preferences", "scalable"),
            os.path.join(theme_dir, "mimetypes", "scalable"),
            os.path.join(theme_dir, "actions", "scalable"),
            os.path.join(theme_dir, "status", "scalable"),
            theme_dir,
        ]
        for base in prio_dirs:
            if not os.path.isdir(base):
                continue
            for root, _, files in os.walk(base):
                for f in files:
                    if not f.lower().endswith((".svg", ".png")):
                        continue
                    stem = os.path.splitext(f)[0].lower()
                    if stem in idx:
                        continue
                    full = os.path.join(root, f)
                    real = self._resolve_symlink_text(full)
                    if os.path.isfile(real) and os.path.getsize(real) >= 60:
                        idx[stem] = real
        return idx

    def _lookup(self, idx, n):
        if not idx:
            return ""
        if n in idx:
            return idx[n]
        if n.endswith("-symbolic"):
            base = n[:-9]
            if base in idx:
                return idx[base]
            n = base
        aliases = {
            "folder-download": "aptdaemon-download",
            "folder-downloads": "aptdaemon-download",
            "download": "aptdaemon-download",
            "system-software-update": "update-manager",
            "cachy-update-blue": "system-software-update",
            "cachy-update_updates-available-blue": "system-software-update",
            "utilities-system-monitor": "ksysguard",
            "window": "preferences-system-windows",
            "terminal": "utilities-terminal",
            "color-picker": "kcolorchooser",
            "document-open-recent": "document-open-recent",
            "org.kde.plasma.clipboard": "klipper",
            "system-bios": "cpu",
            "sleep": "system-suspend",
            "process-stop": "system-shutdown",
            "input-keyboard": "cs-keyboard",
            "screenshot-region": "gnome-screenshot",
            "screenshot-fullscreen": "gnome-screenshot",
            "screenshot-record": "gnome-screenshot",
            "color-mode-dark": "redshift-status-on",
            "color-mode-light": "cs-color",
            # MIME / file-type aliases so active theme stays unified
            "image-png": "image-x-generic",
            "image-jpeg": "image-x-generic",
            "image-jpg": "image-x-generic",
            "image-gif": "image-x-generic",
            "image-webp": "image-x-generic",
            "image-bmp": "image-x-generic",
            "image-svg+xml": "image-x-generic",
            "image-x-icon": "image-x-generic",
            "video-mp4": "video-x-generic",
            "video-x-matroska": "video-x-generic",
            "video-webm": "video-x-generic",
            "video-quicktime": "video-x-generic",
            "video-x-msvideo": "video-x-generic",
            "audio-mp3": "audio-x-generic",
            "audio-mpeg": "audio-x-generic",
            "audio-flac": "audio-x-generic",
            "audio-wav": "audio-x-generic",
            "audio-x-wav": "audio-x-generic",
            "audio-ogg": "audio-x-generic",
            "application-zip": "package-x-generic",
            "application-x-zip-compressed": "package-x-generic",
            "application-x-tar": "package-x-generic",
            "application-gzip": "package-x-generic",
            "application-x-7z-compressed": "package-x-generic",
            "application-x-rar": "package-x-generic",
            "application-vnd.rar": "package-x-generic",
            "application-octet-stream": "text-plain",
            "application-xml": "text-x-generic",
            "text-xml": "text-x-generic",
            "text-csv": "application-vnd.ms-excel",
            "text-html": "text-x-generic",
            "text-css": "text-x-generic",
            "text-javascript": "text-x-generic",
            "application-javascript": "text-x-generic",
            "text-x-script.python": "text-x-python",
            "application-x-msdownload": "application-x-executable",
            "application-x-msi": "application-x-executable",
            "application-x-dosexec": "application-x-executable",
            "application-x-desktop": "application-x-executable",
            "application-vnd.openxmlformats-officedocument.wordprocessingml.document": "application-msword",
            "application-rtf": "application-msword",
            "application-vnd.openxmlformats-officedocument.spreadsheetml.sheet": "application-vnd.ms-excel",
            "application-vnd.openxmlformats-officedocument.presentationml.presentation": "application-vnd.ms-powerpoint",
        }
        if n in aliases and aliases[n] in idx:
            return idx[aliases[n]]
        for cand in (n.replace("_", "-"), n.replace("-", "")):
            if cand in idx:
                return idx[cand]
        # Prefix fallbacks inside the SAME theme before dropping to another theme
        for prefix, fallback in (
            ("image-", "image-x-generic"),
            ("video-", "video-x-generic"),
            ("audio-", "audio-x-generic"),
            ("text-", "text-plain"),
            ("folder-", "folder"),
        ):
            if n.startswith(prefix) and fallback in idx:
                return idx[fallback]
        return ""

    def resolve(self, name, theme_name=None):
        if not name:
            return ""
        n = name.strip().lower()
        # 1. Check requested theme first (e.g. fluent-win11)
        if theme_name and theme_name in self._themes:
            hit = self._lookup(self._themes[theme_name], n)
            if hit:
                return hit
        # 2. Fallback to candy-icons, then fluent-win11
        for fb in ("candy-icons", "fluent-win11"):
            if fb == theme_name or fb not in self._themes:
                continue
            hit = self._lookup(self._themes[fb], n)
            if hit:
                return hit
        return ""


# ── 10. File Helpers (Properties, Archive, Compress, Open With) ──────────────
def format_size(bytes_val):
    units = ("B", "KB", "MB", "GB", "TB")
    v = float(bytes_val)
    u = 0
    while v >= 1024.0 and u < 4:
        v /= 1024.0
        u += 1
    return f"{int(bytes_val)} B" if u == 0 else f"{v:.1f} {units[u]}"


def get_file_properties(raw_path):
    p = os.path.normpath(os.path.expandvars(os.path.expanduser(raw_path or "")))
    if not os.path.exists(p):
        return {"name": os.path.basename(p), "path": p, "isDir": False, "sizeText": "--",
                "mime": "", "mimeComment": "Not found", "modified": "--", "permissions": "---", "owner": ""}
    is_dir = os.path.isdir(p)
    is_link = os.path.islink(p) or p.lower().endswith(".lnk")
    target = resolve_lnk_target(p) if p.lower().endswith(".lnk") else (os.path.realpath(p) if is_link else "")
    st = os.stat(p)
    dt = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%d.%m.%Y %H:%M")
    if is_dir:
        try:
            items = len(os.listdir(p))
        except OSError:
            items = 0
        mime = "inode/directory"
        mime_comment = "Folder"
        size_val = -1
        size_text = f"{items} item" + ("" if items == 1 else "s")
    else:
        mime, _ = mimetypes.guess_type(p)
        mime = mime or "application/octet-stream"
        ext = os.path.splitext(p)[1].lstrip(".").upper()
        mime_comment = f"{ext} File" if ext else "File"
        items = 0
        size_val = st.st_size
        size_text = format_size(st.st_size)
    ro = not os.access(p, os.W_OK)
    return {
        "name": os.path.basename(p.rstrip("\\/")) or p,
        "path": os.path.abspath(p),
        "isDir": is_dir,
        "isSymlink": is_link,
        "target": target,
        "mime": mime,
        "mimeComment": mime_comment,
        "size": size_val,
        "items": items,
        "sizeText": size_text,
        "modified": dt,
        "permissions": "r--" if ro else "rw-",
        "owner": os.environ.get("USERNAME", ""),
    }


def rename_file_path(raw_path, new_name):
    p = os.path.normpath(os.path.expandvars(os.path.expanduser(raw_path or "")))
    if not os.path.exists(p):
        return "File not found"
    new_name = (new_name or "").strip()
    if not new_name:
        return "Name cannot be empty"
    if any(c in new_name for c in '<>:"/\\|?*') or new_name in (".", ".."):
        return "Invalid characters in file name"
    cur_name = os.path.basename(p.rstrip("\\/"))
    if new_name == cur_name:
        return ""
    dst = os.path.join(os.path.dirname(p.rstrip("\\/")), new_name)
    if os.path.exists(dst):
        return "A file or folder with this name already exists"
    try:
        os.rename(p, dst)
        return ""
    except OSError as e:
        return f"Rename failed: {e}"


ARCHIVE_EXTS = (".zip", ".tar", ".gz", ".tgz", ".bz2", ".xz", ".7z", ".rar", ".zst")


def is_archive_path(raw_path):
    p = os.path.expandvars(os.path.expanduser(raw_path or ""))
    if not os.path.isfile(p):
        return False
    return p.lower().endswith(ARCHIVE_EXTS)


def extract_archive_here(raw_path):
    p = os.path.normpath(os.path.expandvars(os.path.expanduser(raw_path or "")))
    if not os.path.isfile(p):
        return
    base = os.path.splitext(p)[0]
    if base.lower().endswith(".tar"):
        base = os.path.splitext(base)[0]
    dest = base
    i = 2
    while os.path.exists(dest):
        dest = f"{base} ({i})"
        i += 1
    os.makedirs(dest, exist_ok=True)
    def _worker():
        try:
            dest_real = os.path.realpath(dest)
            if p.lower().endswith(".zip"):
                with zipfile.ZipFile(p, "r") as zf:
                    for member in zf.infolist():
                        target = os.path.realpath(os.path.join(dest_real, member.filename))
                        if not (target == dest_real or target.startswith(dest_real + os.sep)):
                            continue  # Skip Zip-Slip traversal entries
                        zf.extract(member, dest_real)
            elif shutil.which("7z") or os.path.isfile(r"C:\Program Files\7-Zip\7z.exe"):
                exe = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
                subprocess.run([exe, "x", p, f"-o{dest_real}", "-y"], **_hidden_kwargs())
            else:
                subprocess.run(["tar.exe", "-xf", p, "-C", dest_real], **_hidden_kwargs())
        except Exception:
            pass
    threading.Thread(target=_worker, daemon=True).start()


def list_windows_steam_games():
    """Scan Steam libraryfolders.vdf + appmanifest_*.acf for installed Steam games on Windows."""
    if not IS_WINDOWS:
        return []
    import winreg
    steam_path = ""
    for root_key, sub in (
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
    ):
        try:
            with winreg.OpenKey(root_key, sub) as k:
                val, _ = winreg.QueryValueEx(k, "SteamPath")
                if val and os.path.isdir(val):
                    steam_path = val
                    break
        except Exception:
            continue
    if not steam_path:
        return []
    libs = [os.path.normpath(steam_path)]
    vdf = os.path.join(steam_path, "steamapps", "libraryfolders.vdf")
    if os.path.isfile(vdf):
        try:
            with open(vdf, "r", encoding="utf-8", errors="replace") as f:
                for m in re.finditer(r'"path"\s+"([^"]+)"', f.read()):
                    p = os.path.normpath(m.group(1).replace("\\\\", "\\"))
                    if os.path.isdir(p) and p not in libs:
                        libs.append(p)
        except Exception:
            pass
    games = []
    seen = set()
    for lib in libs:
        sa = os.path.join(lib, "steamapps")
        if not os.path.isdir(sa):
            continue
        try:
            for fn in os.listdir(sa):
                if not (fn.startswith("appmanifest_") and fn.endswith(".acf")):
                    continue
                full = os.path.join(sa, fn)
                with open(full, "r", encoding="utf-8", errors="replace") as f:
                    txt = f.read()
                m_id = re.search(r'"appid"\s+"(\d+)"', txt)
                m_nm = re.search(r'"name"\s+"([^"]+)"', txt)
                if not m_id or not m_nm:
                    continue
                aid, name = m_id.group(1), m_nm.group(1).strip()
                if aid in seen or any(skip in name.lower() for skip in ("steamworks", "proton", "steam linux runtime")):
                    continue
                seen.add(aid)
                games.append({
                    "name": name,
                    "path": f"steam://rungameid/{aid}",
                    "is_dir": False,
                    "mtime": os.path.getmtime(full),
                    "desc": f"Steam App {aid}",
                    "iconName": "steam",
                })
        except Exception:
            continue
    games.sort(key=lambda x: fold_text(x["name"]))
    return games


def compress_to_zip(raw_path):
    p = os.path.normpath(os.path.expandvars(os.path.expanduser(raw_path or "")))
    if not os.path.exists(p):
        return
    parent = os.path.dirname(p.rstrip("\\/"))
    stem = os.path.basename(p.rstrip("\\/"))
    zip_path = os.path.join(parent, f"{stem}.zip")
    i = 2
    while os.path.exists(zip_path):
        zip_path = os.path.join(parent, f"{stem} ({i}).zip")
        i += 1
    def _worker():
        try:
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                if os.path.isfile(p):
                    zf.write(p, arcname=os.path.basename(p))
                else:
                    for root, _, files in os.walk(p):
                        for fn in files:
                            full = os.path.join(root, fn)
                            rel = os.path.relpath(full, parent)
                            zf.write(full, arcname=rel)
        except Exception:
            pass
    threading.Thread(target=_worker, daemon=True).start()


def list_open_with_apps(raw_path):
    """Return practical Windows 'Open With' choices + Windows Open With dialog."""
    out = [{"file": "__WIN_OPENWITH_DIALOG__", "name": "Choose another app… (Windows)", "icon": "applications-all", "isDefault": False}]
    cands = [
        ("Notepad", shutil.which("notepad.exe") or r"C:\Windows\System32\notepad.exe"),
        ("VS Code", shutil.which("code") or os.path.expandvars(r"%LOCALAPPDATA%\Programs\Microsoft VS Code\Code.exe")),
        ("Windows Terminal", shutil.which("wt.exe") or ""),
        ("Microsoft Edge", r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
        ("Google Chrome", r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
        ("Paint", shutil.which("mspaint.exe") or r"C:\Windows\System32\mspaint.exe"),
        ("VLC", r"C:\Program Files\VideoLAN\VLC\vlc.exe"),
    ]
    for name, exe in cands:
        if exe and os.path.isfile(exe):
            out.append({"file": exe, "name": name, "icon": exe, "isDefault": False})
    return out


def open_path_with(raw_path, app_exe):
    p = os.path.normpath(os.path.expandvars(os.path.expanduser(raw_path or "")))
    if not p:
        return
    if app_exe == "__WIN_OPENWITH_DIALOG__":
        subprocess.Popen(["rundll32.exe", "shell32.dll,OpenAs_RunDLL", p])
        return
    if app_exe and os.path.isfile(app_exe):
        subprocess.Popen([app_exe, p])


# ── 11. Native Qt Screen Color Picker Overlay ────────────────────────────────
def launch_color_picker(on_picked, on_closed=None):
    """Show a frameless fullscreen Qt eyedropper across all monitors; calls on_picked('#RRGGBB')."""
    from PySide6.QtCore import Qt, QRect, QPoint
    from PySide6.QtGui import QGuiApplication, QPainter, QColor, QPen, QFont, QPixmap
    from PySide6.QtWidgets import QWidget

    screens = QGuiApplication.screens()
    if not screens:
        return None

    vrect = QRect()
    for s in screens:
        vrect = vrect.united(s.geometry())

    # Composite all screens cleanly in logical virtual-screen coordinates (DPI-safe)
    full_pm = QPixmap(vrect.size())
    full_pm.setDevicePixelRatio(1.0)
    full_pm.fill(Qt.black)
    p = QPainter(full_pm)
    for s in screens:
        g = s.geometry()
        pm = s.grabWindow(0)
        p.drawPixmap(QRect(g.x() - vrect.x(), g.y() - vrect.y(), g.width(), g.height()), pm)
    p.end()
    full_img = full_pm.toImage()

    class ColorPickerOverlay(QWidget):
        def __init__(self):
            super().__init__(None, Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
            self.setAttribute(Qt.WA_DeleteOnClose, True)
            self.setGeometry(vrect)
            self.setMouseTracking(True)
            self.setCursor(Qt.CrossCursor)
            self._pm = full_pm
            self._img = full_img
            self._pos = QPoint(vrect.width() // 2, vrect.height() // 2)
            self._hex = "#000000"

        def closeEvent(self, ev):
            super().closeEvent(ev)
            if on_closed:
                on_closed()

        def _sample(self, pt):
            x = max(0, min(self._img.width() - 1, pt.x()))
            y = max(0, min(self._img.height() - 1, pt.y()))
            c = QColor(self._img.pixel(x, y))
            return c, c.name().upper()

        def mouseMoveEvent(self, ev):
            self._pos = ev.position().toPoint()
            _, self._hex = self._sample(self._pos)
            self.update()

        def mousePressEvent(self, ev):
            if ev.button() == Qt.LeftButton:
                _, hx = self._sample(ev.position().toPoint())
                self.close()
                if on_picked:
                    on_picked(hx)
            else:
                self.close()

        def keyPressEvent(self, ev):
            if ev.key() in (Qt.Key_Escape, Qt.Key_Q):
                self.close()

        def paintEvent(self, ev):
            pt = QPainter(self)
            pt.drawPixmap(0, 0, self._pm)
            c, hx = self._sample(self._pos)
            # Draw zoomed loupe near cursor
            lx = self._pos.x() + 24
            ly = self._pos.y() + 24
            if lx + 130 > self.width():
                lx = self._pos.x() - 144
            if ly + 150 > self.height():
                ly = self._pos.y() - 164
            # Crop 15x15 pixels around cursor and scale 8x
            sx = max(0, min(self._pm.width() - 15, self._pos.x() - 7))
            sy = max(0, min(self._pm.height() - 15, self._pos.y() - 7))
            crop = self._pm.copy(sx, sy, 15, 15).scaled(120, 120)
            pt.fillRect(lx - 4, ly - 4, 128, 156, QColor(18, 20, 26, 235))
            pt.drawPixmap(lx, ly, crop)
            pt.setPen(QPen(QColor(255, 255, 255, 220), 2))
            pt.drawRect(lx + 56, ly + 56, 8, 8)
            pt.setPen(QPen(QColor(255, 255, 255, 140), 1))
            pt.drawRect(lx - 4, ly - 4, 128, 156)
            pt.fillRect(lx + 4, ly + 126, 20, 20, c)
            pt.setPen(QColor(245, 247, 250))
            pt.setFont(QFont("Consolas", 11, QFont.Bold))
            pt.drawText(lx + 30, ly + 141, hx)
            pt.end()

    w = ColorPickerOverlay()
    w.show()
    w.activateWindow()
    w.raise_()
    return w
