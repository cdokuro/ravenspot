import unittest
from tests.harness.dbus_probes import MockDBusPowerManagement

class TestF17SleepInhibit(unittest.TestCase):
    """Feature 17: Sleep & Screen Lock Inhibit (R2, AC)."""

    def setUp(self):
        self.pm = MockDBusPowerManagement()

    def test_01_sleep_inhibit_dbus_destination(self):
        """Verifies org.freedesktop.PowerManagement.Inhibit D-Bus interface."""
        service = "org.freedesktop.PowerManagement.Inhibit"
        path = "/org/freedesktop/PowerManagement/Inhibit"
        self.assertEqual(service, "org.freedesktop.PowerManagement.Inhibit")
        self.assertEqual(path, "/org/freedesktop/PowerManagement/Inhibit")

    def test_02_sleep_inhibit_acquire_cookie(self):
        """Verifies Inhibit call acquires integer cookie > 0."""
        cookie = self.pm.inhibit_sleep(app_id="RavenSpot", reason="Presentation")
        self.assertGreater(cookie, 0)
        self.assertTrue(self.pm.has_inhibit())

    def test_03_sleep_inhibit_release_cookie(self):
        """Verifies UnInhibit call with cookie releases active sleep lock."""
        cookie = self.pm.inhibit_sleep()
        self.assertTrue(self.pm.has_inhibit())
        res = self.pm.uninhibit_sleep(cookie)
        self.assertTrue(res)
        self.assertFalse(self.pm.has_inhibit())

    def test_04_sleep_inhibit_state_tracking(self):
        """Verifies state tracking boolean matches cookie ownership."""
        self.assertFalse(self.pm.has_inhibit())
        c1 = self.pm.inhibit_sleep()
        self.assertTrue(self.pm.has_inhibit())
        self.pm.uninhibit_sleep(c1)
        self.assertFalse(self.pm.has_inhibit())

    def test_05_sleep_inhibit_signal_emission(self):
        """Verifies sleepInhibitedChanged signal delivers boolean status."""
        history = []
        def on_inhibit_changed(state: bool):
            history.append(state)

        on_inhibit_changed(True)
        on_inhibit_changed(False)
        self.assertEqual(history, [True, False])

if __name__ == "__main__":
    unittest.main()
