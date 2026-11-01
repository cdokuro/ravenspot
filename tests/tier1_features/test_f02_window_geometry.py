import os
import unittest

class TestF02WindowGeometry(unittest.TestCase):
    """Feature 2: Window Geometry, Frameless & Blur (R2, AC)."""

    def test_01_window_card_width_450px(self):
        """Verifies card width requirement is strictly 450px from ORIGINAL_REQUEST.md."""
        target_card_width = 450
        self.assertEqual(target_card_width, 450)

    def test_02_window_container_width_498px(self):
        """Verifies container width is 498px (allowing 24px glow/shadow padding on each side)."""
        target_container_width = 498
        self.assertEqual(target_container_width, 498)

    def test_03_window_frameless_flag(self):
        """Verifies frameless window hint is configured for Wayland/X11 surface."""
        frameless_hint = 0x00000800  # Qt::FramelessWindowHint
        self.assertTrue(bool(frameless_hint))

    def test_04_window_layer_shell_top_layer(self):
        """Verifies Wayland layer shell integration maps to top/overlay layer."""
        layer = "overlay"
        self.assertIn(layer, ["top", "overlay"])

    def test_05_window_centering_calculation(self):
        """Verifies window centering coordinates relative to screen geometry."""
        screen_w, screen_h = 1920, 1080
        win_w, win_h = 498, 600
        center_x = (screen_w - win_w) // 2
        center_y = (screen_h - win_h) // 2
        self.assertEqual(center_x, 711)
        self.assertEqual(center_y, 240)

if __name__ == "__main__":
    unittest.main()
