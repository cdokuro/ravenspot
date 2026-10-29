import os
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from PySide6.QtGui import QGuiApplication
app = QGuiApplication.instance() or QGuiApplication(sys.argv)
import bridge

class TestTelemetryAbstraction(unittest.TestCase):
    def setUp(self):
        self.shell = bridge.Shell(start_hidden=True)

    def test_get_sys_monitor_info_structure(self):
        info = self.shell.getSysMonitorInfo()
        self.assertIsInstance(info, dict)
        self.assertIn("ram_text", info)
        self.assertIn("ram_pct", info)
        self.assertIn("disk_text", info)
        self.assertIn("disk_pct", info)
        self.assertIn("uptime", info)

    def test_audio_info_structure(self):
        info = self.shell.getInitialAudioInfo()
        self.assertIsInstance(info, dict)
        self.assertIn("volume", info)
        self.assertIn("muted", info)
        self.assertTrue(0 <= info["volume"] <= 100)
        self.assertIsInstance(info["muted"], bool)

if __name__ == "__main__":
    unittest.main()
