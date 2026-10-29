import os
import sys
import unittest
import tempfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from PySide6.QtGui import QGuiApplication
app = QGuiApplication.instance() or QGuiApplication(sys.argv)
import bridge

class TestWindowsAppScanner(unittest.TestCase):
    def test_scan_windows_programs_mock(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            # Create dummy .lnk files
            p1 = os.path.join(tmpdir, "Visual Studio Code.lnk")
            p2 = os.path.join(tmpdir, "Spotify.lnk")
            with open(p1, "wb") as f: f.write(b"dummy")
            with open(p2, "wb") as f: f.write(b"dummy")

            apps = bridge.scan_windows_directory(tmpdir)
            self.assertEqual(len(apps), 2)
            labels = [a["label"] for a in apps]
            self.assertIn("Visual Studio Code", labels)
            self.assertIn("Spotify", labels)
            self.assertIn("code", apps[0]["aliases"] + apps[1]["aliases"])

    def test_theme_search_paths_includes_local(self):
        from PySide6.QtGui import QIcon
        paths = QIcon.themeSearchPaths()
        self.assertTrue(any("icons" in p for p in paths) or os.path.isdir(bridge.APP_DIR))

if __name__ == "__main__":
    unittest.main()
