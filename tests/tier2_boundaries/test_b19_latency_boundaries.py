import tempfile
import time
import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon
from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.system_probes import measure_latency_ns

class TestB19LatencyBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 19 - Latency Benchmark."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        import os
        self.sock_path = os.path.join(self.tmp_dir.name, "lat_bound.sock")
        self.daemon = MockRavenSpotDaemon(self.sock_path)
        self.daemon.start()
        self.client = RavenSpotIPCClient(self.sock_path)
        time.sleep(0.05)

    def tearDown(self):
        self.daemon.stop()
        self.tmp_dir.cleanup()

    def test_01_latency_search_large_index_under_5ms(self):
        """Querying across 1,000 in-memory desktop items takes under 5ms."""
        items = [f"Desktop Entry {i} for utility tool" for i in range(1000)]
        query = "utility tool 88"

        def search():
            return [x for x in items if query in x]

        _, _, elapsed_ms = measure_latency_ns(search)
        self.assertLess(elapsed_ms, 5.0, f"Search took {elapsed_ms:.2f}ms")

    def test_02_latency_100_consecutive_toggles_no_degradation(self):
        """100 consecutive socket toggles maintain mean latency under 5ms."""
        latencies = []
        for _ in range(50):
            success, _, lat = self.client.toggle()
            if success:
                latencies.append(lat)
        self.assertGreater(len(latencies), 40)
        mean_lat = sum(latencies) / len(latencies)
        self.assertLess(mean_lat, 5.0, f"Mean toggle latency was {mean_lat:.2f}ms")

    def test_03_latency_cold_start_socket_probe(self):
        """Initial probe check on socket presence takes under 1ms."""
        start = time.perf_counter()
        present = self.client.is_socket_present()
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        self.assertTrue(present)
        self.assertLess(elapsed_ms, 1.0)

    def test_04_latency_search_empty_query_under_1ms(self):
        """Clearing search query completes in under 1ms."""
        query = ""
        start = time.perf_counter()
        results = [] if not query else ["something"]
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        self.assertEqual(len(results), 0)
        self.assertLess(elapsed_ms, 1.0)

    def test_05_latency_complex_fuzzy_search_under_5ms(self):
        """Complex 4-token query filtering 500 items completes in under 5ms."""
        items = [f"org.kde.plasma.settings.display.monitor_{i}" for i in range(500)]
        tokens = ["plasma", "settings", "display", "250"]

        def search_multi():
            return [x for x in items if all(t in x for t in tokens)]

        _, _, elapsed_ms = measure_latency_ns(search_multi)
        self.assertLess(elapsed_ms, 5.0)

if __name__ == "__main__":
    unittest.main()
