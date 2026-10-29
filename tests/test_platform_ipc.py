import os
import sys
import unittest

# Add repo to path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

class TestPlatformPathsAndIPC(unittest.TestCase):
    def test_app_dir_exists(self):
        import bridge
        self.assertTrue(os.path.isdir(bridge.APP_DIR))
        self.assertTrue(os.path.isfile(os.path.join(bridge.APP_DIR, "Menu.qml")))

    def test_user_config_dir(self):
        import bridge
        self.assertTrue(bool(bridge.USER_CONFIG_DIR))
        if sys.platform == "win32":
            self.assertIn("ravenspot", bridge.USER_CONFIG_DIR.lower())
        else:
            self.assertIn(".config/ravenspot", bridge.USER_CONFIG_DIR)

    def test_server_name(self):
        import bridge
        self.assertTrue(hasattr(bridge, "SERVER_NAME"))
        self.assertTrue(len(bridge.SERVER_NAME) > 0)

if __name__ == "__main__":
    unittest.main()
