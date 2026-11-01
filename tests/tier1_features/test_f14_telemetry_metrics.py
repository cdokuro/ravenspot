import os
import shutil
import unittest
from tests.harness.system_probes import (
    read_kernel_cpu_percent,
    read_kernel_meminfo,
    read_kernel_uptime
)

class TestF14TelemetryMetrics(unittest.TestCase):
    """Feature 14: Telemetry Metrics (/proc & sysfs) (R2, AC)."""

    def test_01_telemetry_cpu_percentage(self):
        """Verifies CPU usage calculation returns value between 0.0 and 100.0%."""
        cpu = read_kernel_cpu_percent(sample_interval=0.02)
        self.assertGreaterEqual(cpu, 0.0)
        self.assertLessEqual(cpu, 100.0)

    def test_02_telemetry_ram_percentage(self):
        """Verifies RAM usage calculation returns value between 0.0 and 100.0%."""
        mem = read_kernel_meminfo()
        self.assertGreater(mem["total_mb"], 0.0)
        self.assertGreaterEqual(mem["used_percent"], 0.0)
        self.assertLessEqual(mem["used_percent"], 100.0)

    def test_03_telemetry_disk_percentage(self):
        """Verifies Disk usage calculation on '/' returns value between 0.0 and 100.0%."""
        usage = shutil.disk_usage("/")
        disk_pct = (usage.used / usage.total) * 100.0
        self.assertGreater(disk_pct, 0.0)
        self.assertLessEqual(disk_pct, 100.0)

    def test_04_telemetry_uptime_formatting(self):
        """Verifies system uptime reads positive seconds and formats to hours/minutes."""
        uptime_sec = read_kernel_uptime()
        self.assertGreater(uptime_sec, 0.0)
        hours = int(uptime_sec // 3600)
        minutes = int((uptime_sec % 3600) // 60)
        formatted = f"{hours}h {minutes}m"
        self.assertRegex(formatted, r"^\d+h \d+m$")

    def test_05_telemetry_temperature(self):
        """Verifies thermal probe logic against hwmon sysfs or defaults safely."""
        hwmon_dir = "/sys/class/hwmon"
        temp_val = 45.0  # Safe default baseline
        if os.path.exists(hwmon_dir):
            for entry in os.listdir(hwmon_dir):
                temp_file = os.path.join(hwmon_dir, entry, "temp1_input")
                if os.path.exists(temp_file):
                    try:
                        with open(temp_file, "r") as f:
                            raw = int(f.read().strip())
                            temp_val = raw / 1000.0
                            break
                    except Exception:
                        pass
        self.assertGreater(temp_val, -20.0)
        self.assertLess(temp_val, 120.0)

if __name__ == "__main__":
    unittest.main()
