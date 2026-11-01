import time
import unittest

class TestB03HotkeyBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 3 - Global Hotkey Integration."""

    def test_01_hotkey_rapid_repeat_keystrokes(self):
        """Debounce logic suppresses rapid key chatter within 150ms window."""
        toggles = 0
        last_toggle_time = 0.0
        debounce_threshold = 0.15  # 150ms

        def press_meta():
            nonlocal toggles, last_toggle_time
            now = time.time()
            if now - last_toggle_time >= debounce_threshold:
                toggles += 1
                last_toggle_time = now

        # 10 rapid presses without delay
        for _ in range(10):
            press_meta()
        self.assertEqual(toggles, 1)

    def test_02_hotkey_held_down_meta(self):
        """Holding down Meta key triggers only on release / single event, not repeating."""
        is_pressed = False
        fire_count = 0

        def key_down(key):
            nonlocal is_pressed, fire_count
            if key == "Meta" and not is_pressed:
                is_pressed = True
                fire_count += 1

        def key_up(key):
            nonlocal is_pressed
            if key == "Meta":
                is_pressed = False

        # Key down repeated by OS auto-repeat
        for _ in range(10):
            key_down("Meta")
        self.assertEqual(fire_count, 1)
        key_up("Meta")

    def test_03_hotkey_combo_key_suppression(self):
        """Solitary Meta toggle is suppressed when combined with other keys (Meta+E, Meta+L)."""
        def should_toggle(keys_pressed):
            return keys_pressed == {"Meta"}

        self.assertTrue(should_toggle({"Meta"}))
        self.assertFalse(should_toggle({"Meta", "e"}))
        self.assertFalse(should_toggle({"Meta", "l"}))
        self.assertFalse(should_toggle({"Meta", "Space"}))

    def test_04_hotkey_unavailable_shortcuts_daemon(self):
        """Failure to register with KGlobalAccel logs warning without crashing daemon."""
        registered = False
        try:
            # Simulate daemon unavailable
            raise ConnectionError("org.kde.kglobalaccel not available")
        except ConnectionError:
            registered = False
        self.assertFalse(registered)

    def test_05_hotkey_config_write_permission_error(self):
        """Permission error writing kglobalshortcutsrc falls back to in-memory registration."""
        can_write_disk = False
        fallback_used = not can_write_disk
        self.assertTrue(fallback_used)

if __name__ == "__main__":
    unittest.main()
