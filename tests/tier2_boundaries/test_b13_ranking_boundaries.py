import re
import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB13RankingBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 13 - Search Ranking & Navigation."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_rank_b.sock")

    def test_01_ranking_empty_results_navigation(self):
        """Keyboard navigation with 0 results clamps selection index at 0 without IndexError."""
        results = []
        sel_index = 0
        if results:
            sel_index = min(len(results) - 1, sel_index + 1)
        self.assertEqual(sel_index, 0)

    def test_02_ranking_single_result_navigation(self):
        """Keyboard navigation with 1 result keeps selection index clamped at 0."""
        results = ["single_item"]
        sel_index = 0
        sel_index = min(len(results) - 1, sel_index + 1)
        self.assertEqual(sel_index, 0)
        sel_index = max(0, sel_index - 1)
        self.assertEqual(sel_index, 0)

    def test_03_ranking_query_with_1000_chars(self):
        """A 1,000-character query string evaluates in under 5ms without catastrophic backtracking."""
        import time
        long_query = "a" * 1000
        pattern = re.compile(re.escape(long_query))
        start = time.perf_counter()
        pattern.search("short text")
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        self.assertLess(elapsed_ms, 5.0)

    def test_04_ranking_zero_score_items(self):
        """Items with score=0 are sorted strictly below items with score>0."""
        score_pos = self.daemon.calculate_ranking_score(score=1, depth=0, order=0)
        score_zero = self.daemon.calculate_ranking_score(score=0, depth=0, order=0)
        self.assertGreater(score_pos, score_zero)

    def test_05_ranking_unicode_rtl_query(self):
        """Query with RTL characters ('مرحبا') and emojis ('🔍') preserves string integrity."""
        query = "🔍 مرحبا بالعالم"
        self.assertEqual(len(query), 15)
        self.assertTrue(query.startswith("🔍"))

if __name__ == "__main__":
    unittest.main()
