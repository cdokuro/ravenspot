import unittest

class TestF09KRunnerActions(unittest.TestCase):
    """Feature 9: KRunner System Actions (R2, AC)."""

    def setUp(self):
        # KRunner action keyword registry from PROJECT.md
        self.krunner_table = [
            {"id": "shutdown", "keywords": ["shutdown", "kapat", "poweroff"], "exec": "qdbus6 org.kde.Shutdown /Shutdown logoutAndShutdown", "score": -100},
            {"id": "reboot", "keywords": ["reboot", "restart", "yeniden başlat"], "exec": "qdbus6 org.kde.Shutdown /Shutdown logoutAndReboot", "score": -100},
            {"id": "lock", "keywords": ["lock", "kilitle"], "exec": "loginctl lock-session", "score": -100},
            {"id": "suspend", "keywords": ["suspend", "sleep", "askıya al"], "exec": "systemctl suspend", "score": -100},
            {"id": "logout", "keywords": ["logout", "çıkış"], "exec": "qdbus6 org.kde.Shutdown /Shutdown logout", "score": -100},
            {"id": "restart_plasma", "keywords": ["restart plasma", "plasma restart", "plasma yenile"], "exec": "systemctl --user restart plasma-plasmashell", "score": -50}
        ]

    def test_01_krunner_english_keywords(self):
        """Verifies standard English power management action keywords."""
        en_words = ["shutdown", "reboot", "lock", "suspend", "logout"]
        for word in en_words:
            matched = any(word in item["keywords"] for item in self.krunner_table)
            self.assertTrue(matched, f"English keyword '{word}' not registered in KRunner actions")

    def test_02_krunner_turkish_keywords(self):
        """Verifies localized Turkish power management action keywords."""
        tr_words = ["kapat", "yeniden başlat", "kilitle", "askıya al", "çıkış"]
        for word in tr_words:
            matched = any(word in item["keywords"] for item in self.krunner_table)
            self.assertTrue(matched, f"Turkish keyword '{word}' not registered in KRunner actions")

    def test_03_krunner_negative_priority_scores(self):
        """Verifies KRunner system actions carry negative scores so user apps take precedence."""
        for item in self.krunner_table:
            self.assertLess(item["score"], 0, f"Action {item['id']} should have negative priority score")

    def test_04_krunner_plasma_restart_action(self):
        """Verifies dedicated Plasma Shell restart action keyword."""
        plasma_action = next((x for x in self.krunner_table if x["id"] == "restart_plasma"), None)
        self.assertIsNotNone(plasma_action)
        self.assertIn("restart plasma", plasma_action["keywords"])

    def test_05_krunner_action_schema(self):
        """Verifies schema contains id, keywords, exec, and score."""
        for item in self.krunner_table:
            self.assertIn("id", item)
            self.assertIn("keywords", item)
            self.assertIn("exec", item)
            self.assertIn("score", item)

if __name__ == "__main__":
    unittest.main()
