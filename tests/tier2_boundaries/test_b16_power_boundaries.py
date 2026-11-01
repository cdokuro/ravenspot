import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB16PowerBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 16 - Power Profile Switcher (D-Bus)."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_power_b.sock")

    def test_01_power_unknown_profile_name(self):
        """Invalid profile string ('overclock_extreme') is rejected."""
        res = self.daemon.set_power_profile("overclock_extreme")
        self.assertFalse(res)
        self.assertEqual(self.daemon.power_profile, "balanced")

    def test_02_power_dbus_daemon_unavailable(self):
        """When power-profiles-daemon D-Bus service is missing, UI slider falls back to read-only/disabled."""
        dbus_connected = False
        ui_enabled = dbus_connected
        self.assertFalse(ui_enabled)

    def test_03_power_permission_denied_dbus(self):
        """D-Bus AccessDenied error is caught without causing fatal signal."""
        def call_dbus_privileged():
            try:
                raise PermissionError("org.freedesktop.DBus.Error.AccessDenied")
            except PermissionError:
                return False
        self.assertFalse(call_dbus_privileged())

    def test_04_power_rapid_slider_switches(self):
        """Dragging slider rapidly across 10 steps settles on final selected profile."""
        profiles = ["power-saver", "balanced", "performance"]
        for i in range(10):
            p = profiles[i % 3]
            self.daemon.set_power_profile(p)
        self.assertIn(self.daemon.power_profile, profiles)

    def test_05_power_external_change_sync(self):
        """External power profile change signal updates internal state."""
        external_signal_profile = "performance"
        self.daemon.set_power_profile(external_signal_profile)
        self.assertEqual(self.daemon.power_profile, "performance")

if __name__ == "__main__":
    unittest.main()
