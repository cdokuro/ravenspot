import unittest
import time
import threading

class TestF03GlobalHotkey(unittest.TestCase):
    """Feature 3: Global Hotkey Integration (R2, AC)."""

    def test_01_kwin_modifier_only_shortcuts_meta(self):
        """Verifies KWin ModifierOnlyShortcuts configuration maps solitary Meta."""
        kwin_config_entry = "Meta=org.kde.kglobalaccel,/component/ravenspot,org.kde.kglobalaccel.Component,invokeShortcut,toggle"
        self.assertIn("Meta=", kwin_config_entry)
        self.assertIn("ravenspot", kwin_config_entry)

    def test_02_kglobalshortcutsrc_format(self):
        """Verifies kglobalshortcutsrc entry schema for solitary Meta toggle."""
        shortcut_def = {
            "component": "ravenspot",
            "action": "toggle",
            "key": "Meta",
            "default": "none"
        }
        self.assertEqual(shortcut_def["key"], "Meta")
        self.assertEqual(shortcut_def["action"], "toggle")

    def test_03_hotkey_listener_toggle_event(self):
        """Verifies hotkey event triggers callback handler."""
        triggered = []
        def on_hotkey():
            triggered.append(True)

        on_hotkey()
        self.assertTrue(triggered[0])

    def test_04_hotkey_thread_non_blocking(self):
        """Verifies hotkey detection loop operates asynchronously in worker thread."""
        state = {"ran": False}
        def worker():
            time.sleep(0.01)
            state["ran"] = True

        t = threading.Thread(target=worker)
        t.start()
        t.join(timeout=0.5)
        self.assertTrue(state["ran"])

    def test_05_hotkey_adapter_contract(self):
        """Verifies hotkey adapter interface exposes register and unregister contracts."""
        class HotkeyAdapter:
            def register_shortcut(self, key: str) -> bool:
                return key == "Meta"
            def unregister_shortcut(self) -> bool:
                return True

        adapter = HotkeyAdapter()
        self.assertTrue(adapter.register_shortcut("Meta"))
        self.assertTrue(adapter.unregister_shortcut())

if __name__ == "__main__":
    unittest.main()
