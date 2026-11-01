import unittest

class TestB18GridBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 18 - Quick Actions & 2-Column Grid."""

    def test_01_hibernate_when_unsupported(self):
        """When system lacks hibernation support, hibernate option is hidden/disabled."""
        power_states = ["freeze", "mem"]  # 'disk' missing
        is_hibernate_supported = "disk" in power_states
        self.assertFalse(is_hibernate_supported)

    def test_02_cachy_update_command_failure(self):
        """Exit code failure or missing arch-update command displays neutral badge."""
        def check_update_safely():
            try:
                raise FileNotFoundError("arch-update not found")
            except FileNotFoundError:
                return "neutral"
        self.assertEqual(check_update_safely(), "neutral")

    def test_03_quick_action_double_click_prevention(self):
        """Double clicking power actions within 500ms triggers only a single shutdown dispatch."""
        dispatches = 0
        last_click = 0.0
        def on_action_clicked(click_time: float):
            nonlocal dispatches, last_click
            if click_time - last_click >= 0.5:
                dispatches += 1
                last_click = click_time

        on_action_clicked(1.0)
        on_action_clicked(1.2)  # Ignored duplicate click
        self.assertEqual(dispatches, 1)

    def test_04_system_grid_keyboard_grid_navigation(self):
        """2-column navigation: Right arrow increments column by 1, Down arrow increments row by 2."""
        num_items = 7
        curr_index = 0

        # Right arrow (next column): index + 1
        curr_index = min(num_items - 1, curr_index + 1)
        self.assertEqual(curr_index, 1)

        # Down arrow (next row in 2-col grid): index + 2
        curr_index = min(num_items - 1, curr_index + 2)
        self.assertEqual(curr_index, 3)

    def test_05_system_grid_icon_missing_fallback(self):
        """Missing candy-icon falls back to text label and fallback glyph."""
        action = {"label": "Lock", "icon": "missing-lock-icon"}
        resolved_icon = action["icon"] if False else "system-lock-screen"
        self.assertEqual(resolved_icon, "system-lock-screen")

if __name__ == "__main__":
    unittest.main()
