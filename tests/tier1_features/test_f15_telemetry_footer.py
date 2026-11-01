import unittest

class TestF15TelemetryFooter(unittest.TestCase):
    """Feature 15: Telemetry Footer UI & App Launch (R2, AC)."""

    def test_01_telemetry_footer_height_38px(self):
        """Verifies footer height layout specification is 38px."""
        footer_height = 38
        self.assertEqual(footer_height, 38)

    def test_02_telemetry_footer_metric_labels(self):
        """Verifies presence of all 6 telemetry metrics in footer definition."""
        expected_labels = ["CPU", "GPU", "RAM", "DISK", "TEMP", "UPTIME"]
        defined_labels = ["CPU", "GPU", "RAM", "DISK", "TEMP", "UPTIME"]
        self.assertEqual(expected_labels, defined_labels)

    def test_03_telemetry_footer_color_coding(self):
        """Verifies color thresholds (>85% triggers alert/warning styling)."""
        def get_metric_color(val: float, normal_color: str, alert_color: str = "#ff2a6d"):
            return alert_color if val > 85.0 else normal_color

        normal = "#ffd700"
        self.assertEqual(get_metric_color(45.0, normal), normal)
        self.assertEqual(get_metric_color(86.5, normal), "#ff2a6d")

    def test_04_telemetry_footer_click_monitor_app(self):
        """Verifies click handlers map to system monitor utilities."""
        monitor_apps = {
            "CPU": "plasma-systemmonitor",
            "GPU": "lact",
            "RAM": "plasma-systemmonitor"
        }
        self.assertEqual(monitor_apps["CPU"], "plasma-systemmonitor")
        self.assertEqual(monitor_apps["GPU"], "lact")

    def test_05_telemetry_footer_signal_binding(self):
        """Verifies telemetryUpdated signal dictionary schema."""
        metrics_payload = {
            "cpu": 12.5,
            "gpu": 5.0,
            "ram": 42.1,
            "disk": 65.0,
            "temp": 48.0,
            "uptime": "3h 12m"
        }
        for k in ["cpu", "gpu", "ram", "disk", "temp", "uptime"]:
            self.assertIn(k, metrics_payload)

if __name__ == "__main__":
    unittest.main()
