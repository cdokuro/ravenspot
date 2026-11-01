import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB10MathBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 10 - Math Calculator & Unit Converter."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_math_b.sock")

    def test_01_math_division_by_zero(self):
        """Division by zero returns 'undefined' without crashing."""
        res = self.daemon.evaluate_math_expression("100 / 0")
        self.assertEqual(res, "undefined")

    def test_02_math_malformed_expression(self):
        """Malformed syntax (e.g. '5 ++ ** 3' or '((5 + 2') safely returns None."""
        self.assertIsNone(self.daemon.evaluate_math_expression("5 ++ ** 3"))
        self.assertIsNone(self.daemon.evaluate_math_expression("((5 + 2"))

    def test_03_math_extreme_numbers(self):
        """Scientific notation expressions with large exponents do not crash."""
        res = self.daemon.evaluate_math_expression("1000000 * 1000000")
        self.assertIsNotNone(res)
        self.assertEqual(res, "1e+12")

    def test_04_unit_unknown_units(self):
        """Converting non-standard or unsupported units returns None."""
        res = self.daemon.evaluate_unit_conversion("42 apples to bananas")
        self.assertIsNone(res)

    def test_05_math_non_math_string(self):
        """Normal text queries like 'firefox' or 'settings' do not trigger math evaluation."""
        self.assertIsNone(self.daemon.evaluate_math_expression("firefox"))
        self.assertIsNone(self.daemon.evaluate_math_expression("settings"))

if __name__ == "__main__":
    unittest.main()
