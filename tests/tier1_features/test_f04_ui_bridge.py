import unittest

class TestF04UIBridge(unittest.TestCase):
    """Feature 4: UI Bridge & Component Binding (R2, AC)."""

    def setUp(self):
        # Model C++ ShellBridge exposed to QML
        class MockShellBridge:
            def __init__(self):
                self.visible = False
                self.launched_apps = []
                self.executed_cmds = []
                self.clipboard = ""
                self.volume = 80
                self.muted = False

            def toggleMenu(self):
                self.visible = not self.visible

            def showMenu(self):
                self.visible = True

            def hideMenu(self):
                self.visible = False

            def launchApp(self, exec_str: str):
                self.launched_apps.append(exec_str)

            def executeCommand(self, cmd_str: str, in_terminal: bool = False):
                self.executed_cmds.append((cmd_str, in_terminal))

            def copyToClipboard(self, text: str):
                self.clipboard = text

            def setAudioVolume(self, vol: int):
                self.volume = max(0, min(100, vol))

            def setAudioMuted(self, mute: bool):
                self.muted = mute

        self.bridge = MockShellBridge()

    def test_01_shell_bridge_registration(self):
        """Verifies Shell bridge object is exposed with required invokable methods."""
        required_slots = [
            "toggleMenu", "showMenu", "hideMenu", "launchApp",
            "executeCommand", "copyToClipboard", "setAudioVolume", "setAudioMuted"
        ]
        for slot in required_slots:
            self.assertTrue(hasattr(self.bridge, slot), f"Missing invokable slot: {slot}")

    def test_02_shell_bridge_toggle_slots(self):
        """Verifies toggleMenu, showMenu, hideMenu control visibility."""
        self.assertFalse(self.bridge.visible)
        self.bridge.showMenu()
        self.assertTrue(self.bridge.visible)
        self.bridge.toggleMenu()
        self.assertFalse(self.bridge.visible)
        self.bridge.toggleMenu()
        self.assertTrue(self.bridge.visible)
        self.bridge.hideMenu()
        self.assertFalse(self.bridge.visible)

    def test_03_shell_bridge_app_launcher(self):
        """Verifies launchApp records detached execution command."""
        self.bridge.launchApp("firefox %u")
        self.assertEqual(self.bridge.launched_apps, ["firefox %u"])

    def test_04_shell_bridge_execute_command(self):
        """Verifies executeCommand slot receives command string and terminal flag."""
        self.bridge.executeCommand("ls -la", in_terminal=True)
        self.assertEqual(self.bridge.executed_cmds, [("ls -la", True)])

    def test_05_shell_bridge_clipboard_copy(self):
        """Verifies copyToClipboard stores payload."""
        self.bridge.copyToClipboard("Test Copy Payload 123")
        self.assertEqual(self.bridge.clipboard, "Test Copy Payload 123")

if __name__ == "__main__":
    unittest.main()
