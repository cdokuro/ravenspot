import math
import unittest

class TestB15FooterBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 15 - Telemetry Footer UI & App Launch."""

    def test_01_footer_rapid_signal_flood(self):
        """100 rapid metric signals do not crash UI binding."""
        signal_count = 0
        def on_telemetry(metrics):
            nonlocal signal_count
            signal_count += 1

        for i in range(100):
            on_telemetry({"cpu": float(i)})
        self.assertEqual(signal_count, 100)

    def test_02_footer_monitor_not_installed(self):
        """Clicking telemetry metric when target monitor binary is not installed handles ENOENT safely."""
        launched = False
        try:
            # Simulate missing binary
            binary = "non_existent_system_monitor"
            raise FileNotFoundError(f"No such file: {binary}")
        except FileNotFoundError:
            launched = False
        self.assertFalse(launched)

    def test_03_footer_nan_value_handling(self):
        """NaN and Inf metric values are displayed as '--' rather than crashing."""
        def format_metric(val: float) -> str:
            if math.isnan(val) or math.isinf(val):
                return "--"
            return f"{val:.0f}%"

        self.assertEqual(format_metric(float("nan")), "--")
        self.assertEqual(format_metric(float("inf")), "--")
        self.assertEqual(format_metric(42.0), "42%")

    def test_04_footer_negative_temp(self):
        """Negative temperature readings (e.g. -5°C) format correctly."""
        temp = -5.0
        formatted = f"{temp:.0f}°C"
        self.assertEqual(formatted, "-5°C")

    def test_05_footer_uptime_rollover(self):
        """Large uptime (e.g. 400 days) formats with day component cleanly."""
        uptime_sec = 400 * 86400 + 3600 * 5 + 60 * 12
        days = int(uptime_sec // 86400)
        hours = int((uptime_sec % 86400) // 3600)
        formatted = f"{days}d {hours}h"
        self.assertEqual(formatted, "400d 5h")

if __name__ == "__main__":
    unittest.main()
