import os
import subprocess
import sys
import time
import unittest

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.system_probes import (
    get_process_rss_mb,
    get_process_statm_mb,
    get_process_pss_mb,
    get_process_dirty_heap_mb
)

class TestF20MemoryFootprint(unittest.TestCase):
    """Feature 20: Memory Footprint (<50MB Idle Private Dirty / PSS) (R3, AC)."""

    spawned_proc = None
    target_pid = None

    @classmethod
    def setUpClass(cls):
        """Discover running daemon PID via IPC status probe or spawn on-demand daemon."""
        client = RavenSpotIPCClient()
        success, status_data, _ = client.status()
        if success and "pid" in status_data:
            cls.target_pid = int(status_data["pid"])
            cls.spawned_proc = None
        else:
            # Check lock file as fallback
            lock_path = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ravenspot.lock")
            if os.path.exists(lock_path):
                try:
                    with open(lock_path, "r", encoding="utf-8") as f:
                        cls.target_pid = int(f.read().strip())
                except (ValueError, OSError):
                    pass

            if cls.target_pid is None or not os.path.exists(f"/proc/{cls.target_pid}"):
                # Spawn test daemon instance
                bin_path = os.path.join(PROJECT_ROOT, "ravenspot")
                if not os.path.exists(bin_path):
                    raise unittest.SkipTest("ravenspot native binary not compiled")

                cls.spawned_proc = subprocess.Popen([bin_path, "--daemon"], cwd=PROJECT_ROOT)
                # Poll for socket readiness up to 1.5s
                for _ in range(30):
                    time.sleep(0.05)
                    success, status_data, _ = client.status()
                    if success and "pid" in status_data:
                        cls.target_pid = int(status_data["pid"])
                        break
                if cls.target_pid is None:
                    cls.target_pid = cls.spawned_proc.pid

    @classmethod
    def tearDownClass(cls):
        """Cleanly terminate spawned daemon instance if initiated by this test class."""
        if cls.spawned_proc is not None:
            client = RavenSpotIPCClient()
            client.quit()
            try:
                cls.spawned_proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                cls.spawned_proc.kill()
                cls.spawned_proc.wait()

    def test_01_memory_idle_private_dirty_under_50mb(self):
        """Verifies process private dirty memory (heap/stack) is strictly under 50MB threshold (AC target)."""
        dirty_mb = get_process_dirty_heap_mb(self.target_pid)
        self.assertGreater(dirty_mb, 0.0, "Private dirty memory must be non-zero")
        self.assertLess(dirty_mb, 50.0, f"Private dirty memory {dirty_mb:.2f}MB exceeded 50MB budget")

    def test_02_memory_status_vm_rss_reading(self):
        """Verifies /proc/<pid>/status VmRSS parsing logic returns valid resident measurement."""
        rss_mb = get_process_rss_mb(self.target_pid)
        self.assertIsInstance(rss_mb, float)
        self.assertGreater(rss_mb, 0.0)

    def test_03_memory_statm_page_conversion(self):
        """Verifies /proc/<pid>/statm resident page conversion to MB."""
        statm_mb = get_process_statm_mb(self.target_pid)
        self.assertGreater(statm_mb, 0.0)
        self.assertLess(statm_mb, 150.0)

    def test_04_memory_smaps_rollup_pss_validation(self):
        """Verifies proportional set size (PSS) is under 50MB budget."""
        pss_mb = get_process_pss_mb(self.target_pid)
        self.assertGreater(pss_mb, 0.0)
        self.assertLess(pss_mb, 50.0, f"PSS memory {pss_mb:.2f}MB exceeded 50MB")

    def test_05_memory_dirty_heap_bound(self):
        """Verifies private dirty heap pages do not exceed 25MB budget."""
        dirty_mb = get_process_dirty_heap_mb(self.target_pid)
        self.assertGreater(dirty_mb, 0.0)
        self.assertLess(dirty_mb, 25.0, f"Private dirty heap {dirty_mb:.2f}MB exceeded 25MB")

if __name__ == "__main__":
    unittest.main()
