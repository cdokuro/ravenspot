import os
import subprocess
from typing import Optional, List, Dict, Any, Tuple

class MockDBusPowerManagement:
    """
    D-Bus probe and simulator for PowerProfiles and PowerManagement.Inhibit.
    Queries busctl when system bus is available, or provides controlled test mock.
    """

    def __init__(self):
        self.mock_profile = "balanced"
        self.mock_inhibited = False
        self.mock_cookie = 5001
        self.active_cookies = set()

    @staticmethod
    def is_busctl_available() -> bool:
        try:
            res = subprocess.run(["which", "busctl"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return res.returncode == 0
        except Exception:
            return False

    def query_active_power_profile(self) -> str:
        """Query net.hadess.PowerProfiles via busctl, or fallback to mock."""
        if self.is_busctl_available():
            try:
                res = subprocess.run(
                    ["busctl", "get-property", "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles", "ActiveProfile"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=1.0
                )
                if res.returncode == 0 and res.stdout.strip():
                    parts = res.stdout.strip().split()
                    if len(parts) >= 2:
                        return parts[1].strip('"')
            except Exception:
                pass
        return self.mock_profile

    def set_power_profile(self, profile: str) -> bool:
        valid_profiles = ["power-saver", "balanced", "performance"]
        if profile not in valid_profiles:
            return False

        if self.is_busctl_available():
            try:
                res = subprocess.run(
                    ["busctl", "set-property", "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles", "ActiveProfile", "s", profile],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=1.0
                )
                if res.returncode == 0:
                    self.mock_profile = profile
                    return True
            except Exception:
                pass

        self.mock_profile = profile
        return True

    def inhibit_sleep(self, app_id: str = "RavenSpot", reason: str = "User toggle") -> int:
        """Acquire sleep inhibit cookie."""
        if self.is_busctl_available():
            try:
                res = subprocess.run(
                    ["busctl", "--user", "call", "org.freedesktop.PowerManagement.Inhibit", "/org/freedesktop/PowerManagement/Inhibit", "org.freedesktop.PowerManagement.Inhibit", "Inhibit", "ss", app_id, reason],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=1.0
                )
                if res.returncode == 0 and res.stdout.strip():
                    parts = res.stdout.strip().split()
                    if len(parts) >= 2:
                        cookie = int(parts[1])
                        self.active_cookies.add(cookie)
                        self.mock_inhibited = True
                        return cookie
            except Exception:
                pass

        self.mock_cookie += 1
        self.active_cookies.add(self.mock_cookie)
        self.mock_inhibited = True
        return self.mock_cookie

    def uninhibit_sleep(self, cookie: int) -> bool:
        """Release sleep inhibit cookie."""
        if cookie in self.active_cookies:
            self.active_cookies.remove(cookie)
            if not self.active_cookies:
                self.mock_inhibited = False

        if self.is_busctl_available():
            try:
                res = subprocess.run(
                    ["busctl", "--user", "call", "org.freedesktop.PowerManagement.Inhibit", "/org/freedesktop/PowerManagement/Inhibit", "org.freedesktop.PowerManagement.Inhibit", "UnInhibit", "u", str(cookie)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=1.0
                )
                if res.returncode == 0:
                    return True
            except Exception:
                pass

        return True

    def has_inhibit(self) -> bool:
        return self.mock_inhibited or len(self.active_cookies) > 0
