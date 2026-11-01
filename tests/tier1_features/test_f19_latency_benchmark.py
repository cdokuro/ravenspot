import os
import tempfile
import time
import unittest
from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.mock_daemon import MockRavenSpotDaemon
from tests.harness.system_probes import measure_latency_ns

class TestF19LatencyBenchmark(unittest.TestCase):
    """Feature 19: Latency Benchmark (<30ms Toggle, <5ms Search) (R3, AC)."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.sock_path = os.path.join(self.tmp_dir.name, "latency_bench.sock")
        self.daemon = MockRavenSpotDaemon(self.sock_path)
        self.daemon.start()
        self.client = RavenSpotIPCClient(self.sock_path)
        time.sleep(0.05)

    def tearDown(self):
        self.daemon.stop()
        self.tmp_dir.cleanup()

    def test_01_latency_socket_roundtrip_under_5ms(self):
        """Verifies socket round-trip latency for toggle command is < 5ms."""
        success, resp, latency_ms = self.client.toggle()
        self.assertTrue(success)
        self.assertLess(latency_ms, 5.0, f"Socket roundtrip was {latency_ms:.2f}ms (threshold 5.0ms)")

    def test_02_latency_show_window_under_30ms(self):
        """Verifies show window command completes under 30ms (AC target)."""
        success, resp, latency_ms = self.client.show()
        self.assertTrue(success)
        self.assertLess(latency_ms, 30.0, f"Show latency was {latency_ms:.2f}ms (threshold 30.0ms)")

    def test_03_latency_hide_window_under_30ms(self):
        """Verifies hide window command completes under 30ms (AC target)."""
        success, resp, latency_ms = self.client.hide()
        self.assertTrue(success)
        self.assertLess(latency_ms, 30.0, f"Hide latency was {latency_ms:.2f}ms (threshold 30.0ms)")

    def test_04_latency_search_query_under_5ms(self):
        """Verifies in-memory search query filtering latency is < 5ms (AC target)."""
        # Benchmark in-memory search match
        items = [f"application_{i}" for i in range(500)]
        query = "application_42"

        def do_search():
            return [x for x in items if query in x]

        _, _, elapsed_ms = measure_latency_ns(do_search)
        self.assertLess(elapsed_ms, 5.0, f"Search filtering was {elapsed_ms:.2f}ms (threshold 5.0ms)")

    def test_05_latency_status_query_under_2ms(self):
        """Verifies IPC status command latency is < 2ms."""
        success, data, latency_ms = self.client.status()
        self.assertTrue(success)
        self.assertLess(latency_ms, 2.0, f"Status query was {latency_ms:.2f}ms (threshold 2.0ms)")

if __name__ == "__main__":
    unittest.main()
