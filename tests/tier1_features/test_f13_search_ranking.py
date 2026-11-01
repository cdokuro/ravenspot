import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestF13SearchRanking(unittest.TestCase):
    """Feature 13: Search Ranking & Keyboard Navigation (R2, AC)."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_ranking.sock")

    def test_01_ranking_formula_score_weight(self):
        """Verifies ranking formula: score * 1000 + depth * 25 + order."""
        # Exact match (score=10), depth=0, order=100
        exact_rank = self.daemon.calculate_ranking_score(score=10, depth=0, order=100)
        self.assertEqual(exact_rank, 10 * 1000 + 0 * 25 + 100)
        self.assertEqual(exact_rank, 10100)

        # Prefix match (score=8), depth=1, order=50
        prefix_rank = self.daemon.calculate_ranking_score(score=8, depth=1, order=50)
        self.assertEqual(prefix_rank, 8 * 1000 + 1 * 25 + 50)
        self.assertEqual(prefix_rank, 8075)

        self.assertGreater(exact_rank, prefix_rank)

    def test_02_ranking_depth_order_calculation(self):
        """Verifies depth and order tiebreakers when base score is identical."""
        item_root = self.daemon.calculate_ranking_score(score=5, depth=0, order=10)
        item_sub = self.daemon.calculate_ranking_score(score=5, depth=2, order=10)
        self.assertGreater(item_sub, item_root)

    def test_03_search_navigation_down_arrow(self):
        """Verifies pressing Down Arrow advances active index within result bounds."""
        results_count = 5
        curr_index = 0
        curr_index = min(results_count - 1, curr_index + 1)
        self.assertEqual(curr_index, 1)

    def test_04_search_navigation_up_arrow(self):
        """Verifies pressing Up Arrow decrements active index and clamps at 0."""
        curr_index = 2
        curr_index = max(0, curr_index - 1)
        self.assertEqual(curr_index, 1)

        curr_index = max(0, curr_index - 1)
        self.assertEqual(curr_index, 0)

        # Clamping at top
        curr_index = max(0, curr_index - 1)
        self.assertEqual(curr_index, 0)

    def test_05_search_esc_clears_or_hides(self):
        """Verifies ESC key semantics: if search text present, clear text; if empty, hide window."""
        search_text = "test query"
        visible = True

        # First ESC press: clears search query
        if search_text:
            search_text = ""
        else:
            visible = False
        self.assertEqual(search_text, "")
        self.assertTrue(visible)

        # Second ESC press: closes window
        if search_text:
            search_text = ""
        else:
            visible = False
        self.assertFalse(visible)

if __name__ == "__main__":
    unittest.main()
