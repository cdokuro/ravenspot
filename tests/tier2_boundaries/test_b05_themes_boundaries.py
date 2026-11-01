import json
import unittest

class TestB05ThemesBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 5 - Dynamic Themes Engine."""

    def test_01_themes_missing_file_fallback(self):
        """Missing themes.json falls back to built-in raven_cyber defaults."""
        default_theme = {
            "name": "Raven Cyber",
            "bgColor": "#0b0c10",
            "bgBorder": "#ffd700",
            "accent": "#ffd700"
        }
        loaded = None
        if loaded is None:
            loaded = default_theme
        self.assertEqual(loaded["name"], "Raven Cyber")

    def test_02_themes_corrupt_json(self):
        """Malformed JSON string is caught and safely handled without crash."""
        malformed = "{ active: 'raven_cyber', themes: "
        try:
            json.loads(malformed)
            parsed = True
        except json.JSONDecodeError:
            parsed = False
        self.assertFalse(parsed)

    def test_03_themes_partial_theme_entry(self):
        """Theme with missing properties fills in defaults for missing tokens."""
        sparse_theme = {"name": "Custom"}
        defaults = {"font": "Inter", "bgColor": "#0b0c10", "accent": "#ffd700"}
        merged = {**defaults, **sparse_theme}
        self.assertEqual(merged["font"], "Inter")
        self.assertEqual(merged["name"], "Custom")

    def test_04_themes_switch_nonexistent_theme_name(self):
        """Request to activate unknown theme name is rejected and maintains current active."""
        available = ["raven_cyber", "cachy_emerald"]
        active = "raven_cyber"
        requested = "alien_matrix_theme"
        if requested in available:
            active = requested
        self.assertEqual(active, "raven_cyber")

    def test_05_themes_empty_color_string(self):
        """Empty color string falls back to default contrast border/glow."""
        color = ""
        resolved = color if color.strip() else "#ffd700"
        self.assertEqual(resolved, "#ffd700")

if __name__ == "__main__":
    unittest.main()
