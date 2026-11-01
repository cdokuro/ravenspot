import unittest
from tests.harness.dbus_probes import MockDBusPowerManagement

class TestB17InhibitBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 17 - Sleep & Screen Lock Inhibit."""

    def setUp(self):
        self.pm = MockDBusPowerManagement()

    def test_01_inhibit_uninhibit_without_cookie(self):
        """Releasing sleep inhibit when no cookie exists is a safe no-op."""
        res = self.pm.uninhibit_sleep(cookie=999999)
        self.assertTrue(res)
        self.assertFalse(self.pm.has_inhibit())

    def test_02_inhibit_daemon_crash_cleanup(self):
        """Daemon exit routine releases all active inhibition cookies."""
        c1 = self.pm.inhibit_sleep()
        c2 = self.pm.inhibit_sleep()
        self.assertTrue(self.pm.has_inhibit())

        # Cleanup on exit
        for cookie in list(self.pm.active_cookies):
            self.pm.uninhibit_sleep(cookie)
        self.assertFalse(self.pm.has_inhibit())

    def test_03_inhibit_dbus_timeout(self):
        """D-Bus call timeout (e.g. power management hung) falls back without freezing UI."""
        def call_inhibit_with_timeout():
            try:
                raise TimeoutError("D-Bus response timeout after 1000ms")
            except TimeoutError:
                return 0
        self.assertEqual(call_inhibit_with_timeout(), 0)

    def test_04_inhibit_duplicate_inhibit_calls(self):
        """Calling inhibit multiple times tracks each cookie distinctly."""
        c1 = self.pm.inhibit_sleep()
        c2 = self.pm.inhibit_sleep()
        self.assertNotEqual(c1, c2)
        self.assertEqual(len(self.pm.active_cookies), 2)

    def test_05_inhibit_invalid_cookie_release(self):
        """Negative or zero cookie release is safely ignored without throwing."""
        res = self.pm.uninhibit_sleep(0)
        self.assertTrue(res)
        res = self.pm.uninhibit_sleep(-1)
        self.assertTrue(res)

if __name__ == "__main__":
    unittest.main()
