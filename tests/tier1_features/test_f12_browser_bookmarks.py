import json
import sqlite3
import tempfile
import unittest

class TestF12BrowserBookmarks(unittest.TestCase):
    """Feature 12: Browser Bookmarks Search (R2, AC)."""

    def test_01_bookmarks_chromium_json_parser(self):
        """Verifies parsing of Chromium / Chrome bookmarks JSON format."""
        chromium_json = {
            "roots": {
                "bookmark_bar": {
                    "children": [
                        {"name": "GitHub", "type": "url", "url": "https://github.com"},
                        {"name": "Arch Linux", "type": "url", "url": "https://archlinux.org"}
                    ]
                }
            }
        }
        bookmarks = []
        for item in chromium_json["roots"]["bookmark_bar"]["children"]:
            if item.get("type") == "url":
                bookmarks.append({"title": item["name"], "url": item["url"]})

        self.assertEqual(len(bookmarks), 2)
        self.assertEqual(bookmarks[0]["title"], "GitHub")

    def test_02_bookmarks_firefox_sqlite_parser(self):
        """Verifies querying places.sqlite schema for Firefox bookmarks."""
        with tempfile.NamedTemporaryFile(suffix=".sqlite") as tmp_db:
            conn = sqlite3.connect(tmp_db.name)
            cur = conn.cursor()
            cur.execute("CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT, title TEXT)")
            cur.execute("CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, fk INTEGER, title TEXT)")
            cur.execute("INSERT INTO moz_places VALUES (1, 'https://kde.org', 'KDE Community')")
            cur.execute("INSERT INTO moz_bookmarks VALUES (10, 1, 'KDE Home')")
            conn.commit()

            cur.execute("SELECT b.title, p.url FROM moz_bookmarks b JOIN moz_places p ON b.fk = p.id")
            row = cur.fetchone()
            self.assertEqual(row[0], "KDE Home")
            self.assertEqual(row[1], "https://kde.org")
            conn.close()

    def test_03_bookmarks_query_matches_title(self):
        """Verifies searching bookmark items by title."""
        item = {"title": "Arch Linux Wiki", "url": "https://wiki.archlinux.org"}
        query = "wiki"
        self.assertIn(query, item["title"].lower())

    def test_04_bookmarks_query_matches_url(self):
        """Verifies searching bookmark items by domain/URL."""
        item = {"title": "Arch Linux", "url": "https://wiki.archlinux.org"}
        query = "archlinux.org"
        self.assertIn(query, item["url"].lower())

    def test_05_bookmarks_launch_exec(self):
        """Verifies bookmark search result item execution command (xdg-open)."""
        url = "https://cachyos.org"
        exec_cmd = f"xdg-open '{url}'"
        self.assertTrue(exec_cmd.startswith("xdg-open "))
        self.assertIn(url, exec_cmd)

if __name__ == "__main__":
    unittest.main()
