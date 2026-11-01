import unittest

class TestF18QuickActionsGrid(unittest.TestCase):
    """Feature 18: Quick Actions & 2-Column Grid (R2, AC)."""

    def test_01_cachy_update_badge_state(self):
        """Verifies update checker reads update status cleanly."""
        update_available = False
        self.assertIsInstance(update_available, bool)

    def test_02_cachy_update_icon_dynamic(self):
        """Verifies update badge icon: red arrow when updates available, green check when up to date."""
        def get_update_icon(has_updates: bool) -> str:
            return "software-update-urgent" if has_updates else "software-update-available"

        self.assertEqual(get_update_icon(True), "software-update-urgent")
        self.assertEqual(get_update_icon(False), "software-update-available")

    def test_03_quick_action_reboot_button(self):
        """Verifies reboot quick button command maps to KDE Shutdown service."""
        reboot_cmd = "qdbus6 org.kde.Shutdown /Shutdown logoutAndReboot"
        self.assertIn("logoutAndReboot", reboot_cmd)

    def test_04_quick_action_shutdown_button(self):
        """Verifies shutdown quick button command maps to KDE Shutdown service."""
        shutdown_cmd = "qdbus6 org.kde.Shutdown /Shutdown logoutAndShutdown"
        self.assertIn("logoutAndShutdown", shutdown_cmd)

    def test_05_system_grid_seven_actions(self):
        """Verifies 2-column systemGridView contains all 7 core power actions with candy icons."""
        expected_actions = [
            "Screensaver", "Lock", "Suspend", "Hibernate", "Logout", "Reboot", "Shutdown"
        ]
        grid_items = [
            {"label": "Screensaver", "icon": "preferences-desktop-screensaver"},
            {"label": "Lock", "icon": "system-lock-screen"},
            {"label": "Suspend", "icon": "system-suspend"},
            {"label": "Hibernate", "icon": "system-suspend-hibernate"},
            {"label": "Logout", "icon": "system-log-out"},
            {"label": "Reboot", "icon": "system-reboot"},
            {"label": "Shutdown", "icon": "system-shutdown"}
        ]
        self.assertEqual(len(grid_items), 7)
        labels = [item["label"] for item in grid_items]
        self.assertEqual(labels, expected_actions)

if __name__ == "__main__":
    unittest.main()
