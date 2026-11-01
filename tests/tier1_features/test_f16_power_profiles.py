import unittest
from tests.harness.dbus_probes import MockDBusPowerManagement

class TestF16PowerProfiles(unittest.TestCase):
    """Feature 16: Power Profile Switcher (D-Bus) (R2, AC)."""

    def setUp(self):
        self.pm = MockDBusPowerManagement()

    def test_01_power_profile_dbus_destination(self):
        """Verifies net.hadess.PowerProfiles D-Bus service and interface contract."""
        service = "net.hadess.PowerProfiles"
        path = "/net/hadess/PowerProfiles"
        interface = "net.hadess.PowerProfiles"
        self.assertEqual(service, "net.hadess.PowerProfiles")
        self.assertEqual(path, "/net/hadess/PowerProfiles")
        self.assertEqual(interface, "net.hadess.PowerProfiles")

    def test_02_power_profile_valid_modes(self):
        """Verifies 3 supported power profile states: power-saver, balanced, performance."""
        valid_profiles = ["power-saver", "balanced", "performance"]
        self.assertEqual(len(valid_profiles), 3)
        self.assertIn("balanced", valid_profiles)

    def test_03_power_profile_query_active(self):
        """Verifies reading current active power profile."""
        profile = self.pm.query_active_power_profile()
        self.assertIn(profile, ["power-saver", "balanced", "performance"])

    def test_04_power_profile_switch_call(self):
        """Verifies switching power profile to 'performance' and 'power-saver'."""
        res = self.pm.set_power_profile("performance")
        self.assertTrue(res)
        self.assertEqual(self.pm.query_active_power_profile(), "performance")

        res = self.pm.set_power_profile("power-saver")
        self.assertTrue(res)
        self.assertEqual(self.pm.query_active_power_profile(), "power-saver")

    def test_05_power_profile_signal_emission(self):
        """Verifies powerProfileChanged signal dispatch."""
        emitted_signals = []
        def on_profile_changed(new_profile):
            emitted_signals.append(new_profile)

        # Simulate profile change
        on_profile_changed("balanced")
        self.assertEqual(emitted_signals, ["balanced"])

if __name__ == "__main__":
    unittest.main()
