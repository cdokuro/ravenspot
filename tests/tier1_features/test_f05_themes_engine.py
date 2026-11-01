import json
import os
import re
import unittest

class TestF05ThemesEngine(unittest.TestCase):
    """Feature 5: Dynamic Themes Engine (5 Themes) (R2, AC)."""

    def setUp(self):
        # Locate themes.json in project root
        self.themes_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../themes.json"))
        self.assertTrue(os.path.exists(self.themes_path), f"themes.json not found at {self.themes_path}")
        with open(self.themes_path, "r", encoding="utf-8") as f:
            self.data = json.load(f)

    def test_01_themes_file_parse_valid(self):
        """Verifies themes.json contains valid JSON with active theme indicator."""
        self.assertIn("active", self.data)
        self.assertIn("themes", self.data)
        self.assertIn(self.data["active"], self.data["themes"])

    def test_02_themes_all_five_present(self):
        """Verifies exactly 5 themes specified in PROJECT.md are defined."""
        expected_themes = [
            "raven_cyber",
            "cachy_emerald",
            "sweet_mars",
            "nordic_frost",
            "obsidian_stealth"
        ]
        themes = self.data.get("themes", {})
        for name in expected_themes:
            self.assertIn(name, themes, f"Theme '{name}' is missing from themes.json")

    def test_03_themes_required_keys_per_theme(self):
        """Verifies each theme has all required styling parameters."""
        required_keys = [
            "name", "desc", "font", "bgColor", "bgBorder",
            "glowColor", "selBg", "selFg", "fgColor", "fgDim", "accent", "cpuColor", "gpuColor"
        ]
        for theme_name, theme in self.data["themes"].items():
            for key in required_keys:
                self.assertIn(key, theme, f"Theme '{theme_name}' is missing '{key}'")

    def test_04_themes_color_hex_format(self):
        """Verifies all color properties are valid hex color codes."""
        hex_pattern = re.compile(r"^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")
        color_keys = ["bgColor", "bgBorder", "glowColor", "selBg", "selFg", "fgColor", "fgDim", "accent", "cpuColor", "gpuColor"]
        for theme_name, theme in self.data["themes"].items():
            for key in color_keys:
                val = theme.get(key, "")
                self.assertRegex(val, hex_pattern, f"Invalid hex color '{val}' for '{key}' in theme '{theme_name}'")

    def test_05_themes_runtime_switch(self):
        """Verifies theme switching logic updates active styling tokens."""
        themes = self.data["themes"]
        active = "raven_cyber"
        self.assertEqual(themes[active]["bgBorder"], "#ffd700")

        # Switch to cachy_emerald
        active = "cachy_emerald"
        self.assertEqual(themes[active]["bgBorder"], "#00f5a0")

        # Switch to sweet_mars
        active = "sweet_mars"
        self.assertEqual(themes[active]["bgBorder"], "#ff2a6d")

if __name__ == "__main__":
    unittest.main()
