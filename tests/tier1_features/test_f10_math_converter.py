import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestF10MathConverter(unittest.TestCase):
    """Feature 10: Math Calculator & Unit Converter (R2, AC)."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_math.sock")

    def test_01_math_basic_arithmetic(self):
        """Verifies standard arithmetic: '5 + 7 * 3' -> '26'."""
        result = self.daemon.evaluate_math_expression("5 + 7 * 3")
        self.assertEqual(result, "26")

    def test_02_math_percent_expression(self):
        """Verifies percentage calculation: '20% of 150' -> '30'."""
        result = self.daemon.evaluate_math_expression("20% of 150")
        self.assertEqual(result, "30")

    def test_03_unit_length_conversion(self):
        """Verifies length conversion: '10 km to miles'."""
        result = self.daemon.evaluate_unit_conversion("10 km to miles")
        self.assertIsNotNone(result)
        self.assertIn("6.21", result)
        self.assertIn("miles", result)

    def test_04_unit_data_conversion(self):
        """Verifies data unit conversion: '1024 mb to gb' -> '1 GB'."""
        result = self.daemon.evaluate_unit_conversion("1024 mb to gb")
        self.assertEqual(result, "1 GB")

    def test_05_unit_temp_conversion(self):
        """Verifies temperature conversion: '100 c to f' -> '212 °F'."""
        result = self.daemon.evaluate_unit_conversion("100 c to f")
        self.assertEqual(result, "212 °F")

if __name__ == "__main__":
    unittest.main()
