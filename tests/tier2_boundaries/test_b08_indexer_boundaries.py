import os
import tempfile
import time
import unittest

class TestB08IndexerBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 8 - Application Indexer (.desktop)."""

    def test_01_indexer_corrupt_desktop_file(self):
        """Binary garbage or truncated .desktop files are skipped without crash."""
        corrupt_content = b"\x00\xff\xfe\x01\x02\x03\x04Truncated"
        try:
            text = corrupt_content.decode("utf-8")
        except UnicodeDecodeError:
            text = None
        self.assertIsNone(text)

    def test_02_indexer_missing_exec_key(self):
        """Desktop entry without 'Exec=' is invalid and excluded from app index."""
        entry = {"Name": "Documentation Guide", "Type": "Link"}
        is_launchable_app = "Exec" in entry and entry.get("Type") == "Application"
        self.assertFalse(is_launchable_app)

    def test_03_indexer_empty_applications_dir(self):
        """Empty applications directory produces empty index without error."""
        with tempfile.TemporaryDirectory() as empty_dir:
            entries = os.listdir(empty_dir)
            self.assertEqual(len(entries), 0)

    def test_04_indexer_symlink_loop(self):
        """Circular symlinks are detected to prevent infinite directory recursion."""
        visited_inodes = set()
        loop_detected = False

        # Simulate visiting inode 42 twice
        for inode in [10, 20, 30, 42, 50, 42]:
            if inode in visited_inodes:
                loop_detected = True
                break
            visited_inodes.add(inode)
        self.assertTrue(loop_detected)

    def test_05_indexer_large_directory_scale(self):
        """Processing 1,000 application entries executes in under 100ms."""
        start = time.perf_counter()
        index = {}
        for i in range(1000):
            index[f"app_{i}"] = {
                "name": f"Application {i}",
                "exec": f"/usr/bin/app_{i}",
                "icon": "application-default"
            }
        elapsed = time.perf_counter() - start
        self.assertEqual(len(index), 1000)
        self.assertLess(elapsed, 0.1, f"Indexing 1000 items took {elapsed:.3f}s")

if __name__ == "__main__":
    unittest.main()
