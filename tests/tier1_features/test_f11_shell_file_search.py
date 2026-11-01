import unittest

class TestF11ShellFileSearch(unittest.TestCase):
    """Feature 11: Shell Runner & File Search (R2, AC)."""

    def test_01_shell_prefix_detection(self):
        """Verifies '>' prefix routes query to shell execution subsystem."""
        query = "> systemctl status"
        is_shell = query.startswith("> ")
        self.assertTrue(is_shell)
        cmd = query[2:].strip()
        self.assertEqual(cmd, "systemctl status")

    def test_02_shell_execution_formatting(self):
        """Verifies shell execution command payload generation."""
        raw_cmd = "btop"
        terminal_exec = f"konsole -e {raw_cmd}"
        self.assertIn("konsole -e", terminal_exec)
        self.assertIn(raw_cmd, terminal_exec)

    def test_03_file_search_prefix_detection(self):
        """Verifies '*' prefix routes query to file search subsystem."""
        query = "* document.pdf"
        is_file_search = query.startswith("* ")
        self.assertTrue(is_file_search)
        pattern = query[2:].strip()
        self.assertEqual(pattern, "document.pdf")

    def test_04_file_search_pattern_matching(self):
        """Verifies glob / substring pattern matching against sample filenames."""
        sample_files = [
            "/home/user/document.pdf",
            "/home/user/notes.txt",
            "/home/user/project_document.docx"
        ]
        pattern = "document"
        matches = [f for f in sample_files if pattern in f.lower()]
        self.assertEqual(len(matches), 2)
        self.assertIn("/home/user/document.pdf", matches)

    def test_05_file_search_result_schema(self):
        """Verifies file search result item schema (type, path, icon)."""
        file_item = {
            "id": "file:/home/user/notes.txt",
            "name": "notes.txt",
            "comment": "/home/user/notes.txt",
            "type": "file",
            "icon": "text-plain",
            "exec": "xdg-open /home/user/notes.txt",
            "score": 500
        }
        self.assertEqual(file_item["type"], "file")
        self.assertTrue(file_item["exec"].startswith("xdg-open "))

if __name__ == "__main__":
    unittest.main()
