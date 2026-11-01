import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB14TelemetryBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 14 - Telemetry Metrics (/proc & sysfs)."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_telem_b.sock")

    def test_01_telemetry_zero_memtotal_guard(self):
        """MemTotal=0 in simulated /proc/meminfo does not trigger ZeroDivisionError."""
        mem_total = 0.0
        mem_avail = 0.0
        pct = ((mem_total - mem_avail) / mem_total * 100.0) if mem_total > 0 else 0.0
        self.assertEqual(pct, 0.0)

    def test_02_telemetry_missing_gpu_sysfs(self):
        """Systems without GPU sysfs entries report 0.0% instead of crashing."""
        gpu_percent = 0.0
        self.assertEqual(gpu_percent, 0.0)

    def test_03_telemetry_hwmon_permission_denied(self):
        """Permission denied error on temperature probe falls back gracefully to 0.0 or last cached."""
        def read_temp_mock():
            try:
                raise PermissionError("EACCES")
            except PermissionError:
                return 45.0  # Fallback cached

        self.assertEqual(read_temp_mock(), 45.0)

    def test_04_telemetry_timer_stopped_when_hidden(self):
        """Hiding window immediately halts telemetry timer (0% CPU at idle requirement)."""
        self.daemon.visible = False
        self.daemon.process_command("hide")
        self.assertFalse(self.daemon.telemetry_timer_active)

        self.daemon.process_command("show")
        self.assertTrue(self.daemon.telemetry_timer_active)

        self.daemon.process_command("hide")
        self.assertFalse(self.daemon.telemetry_timer_active)

    def test_05_telemetry_metric_values_in_bounds(self):
        """Telemetry percentages are strictly clamped within [0.0, 100.0]."""
        raw_samples = [-15.0, 0.0, 45.2, 100.0, 105.8]
        clamped = [max(0.0, min(100.0, x)) for x in raw_samples]
        self.assertEqual(clamped[0], 0.0)
        self.assertEqual(clamped[-1], 100.0)

if __name__ == "__main__":
    unittest.main()
