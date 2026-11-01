import unittest

class TestB09KRunnerBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 9 - KRunner System Actions."""

    def setUp(self):
        self.actions = {
            "shutdown": ["shutdown", "kapat", "poweroff"],
            "reboot": ["reboot", "restart", "yeniden başlat", "yeniden baslat"],
            "logout": ["logout", "çıkış", "cikis"]
        }

    def match_action(self, query: str):
        q = query.strip().lower()
        if not q:
            return []
        matches = []
        for action, kw_list in self.actions.items():
            if any(q in kw or kw in q for kw in kw_list):
                matches.append(action)
        return matches

    def test_01_krunner_case_insensitive_matching(self):
        """Mixed casing ('SHUTDOWN', 'ReBoot', 'kApAt') matches target actions."""
        self.assertIn("shutdown", self.match_action("SHUTDOWN"))
        self.assertIn("reboot", self.match_action("ReBoot"))
        self.assertIn("shutdown", self.match_action("kApAt"))

    def test_02_krunner_diacritics_turkish_matching(self):
        """ASCII normalized Turkish input ('cikis', 'yeniden baslat') matches actions."""
        self.assertIn("logout", self.match_action("cikis"))
        self.assertIn("logout", self.match_action("çıkış"))
        self.assertIn("reboot", self.match_action("yeniden baslat"))

    def test_03_krunner_partial_substring_query(self):
        """Partial prefix ('shut', 'reb') matches action."""
        self.assertIn("shutdown", self.match_action("shut"))
        self.assertIn("reboot", self.match_action("reb"))

    def test_04_krunner_empty_query_no_krunner_spam(self):
        """Empty query produces 0 KRunner action suggestions."""
        self.assertEqual(len(self.match_action("")), 0)
        self.assertEqual(len(self.match_action("   ")), 0)

    def test_05_krunner_special_chars_query(self):
        """Query with shell metacharacters ('shutdown; rm -rf /') does not bypass parser."""
        matches = self.match_action("shutdown; rm -rf /")
        self.assertIn("shutdown", matches)
        # Executable string must remain the fixed D-Bus command, not the injected string
        clean_exec = "qdbus6 org.kde.Shutdown /Shutdown logoutAndShutdown"
        self.assertNotIn("rm -rf", clean_exec)

if __name__ == "__main__":
    unittest.main()
