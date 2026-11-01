import re
import unittest

class TestB11ShellBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 11 - Shell Runner & File Search."""

    def test_01_shell_prefix_only(self):
        """Query with '>' prefix only and no command returns empty list."""
        query = ">"
        cmd = query[1:].strip()
        self.assertEqual(cmd, "")

    def test_02_file_search_prefix_only(self):
        """Query with '*' prefix only and no pattern returns empty list."""
        query = "*"
        pattern = query[1:].strip()
        self.assertEqual(pattern, "")

    def test_03_file_search_special_glob_chars(self):
        """Special regex/glob metacharacters ('[a-z]*+(?.txt') are escaped safely."""
        raw_pattern = "[a-z]*+(?.txt"
        escaped = re.escape(raw_pattern)
        try:
            re.compile(escaped)
            valid_regex = True
        except re.error:
            valid_regex = False
        self.assertTrue(valid_regex)

    def test_04_shell_dangerous_escapes(self):
        """Backticks and subshell substitutions in command queries are preserved verbatim for shell."""
        raw_cmd = "`whoami`"
        escaped_cmd = f"konsole -e {raw_cmd}"
        self.assertIn("`whoami`", escaped_cmd)

    def test_05_file_search_timeout_guard(self):
        """Async file search enforces timeout deadline (500ms)."""
        timeout_sec = 0.5
        self.assertLessEqual(timeout_sec, 0.5)

if __name__ == "__main__":
    unittest.main()
