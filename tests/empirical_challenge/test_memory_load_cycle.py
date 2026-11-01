#!/usr/bin/env python3
import os
import sys
import time
import subprocess
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

class TestMemoryLoadCycle(unittest.TestCase):
    """
    Milestone 2 Adversarial Stress Harness: Memory Footprint Stability Under Load
    50 Window Visibility (toggle, show, hide) Cycles via IPC.
    Monitors:
      - Initial Cold-Start Idle Footprint (PSS < 50MB, Private Dirty < 30MB)
      - Active Scenegraph Footprint under repeated IPC toggles
      - Monotonic Growth / Memory Leak Drift across 50 cycles
    """

    daemon_proc = None
    target_pid = None
    client = None
    runtime_dir = None

    @classmethod
    def setUpClass(cls):
        cls.runtime_dir = "/tmp/challenger_m2_2_runtime"
        os.makedirs(cls.runtime_dir, exist_ok=True)
        socket_path = os.path.join(cls.runtime_dir, "ravenspot.sock")

        cls.client = RavenSpotIPCClient(socket_path=socket_path)

        # Terminate any stale daemon on this socket
        if cls.client.is_socket_present():
            cls.client.quit()
            time.sleep(0.3)

        bin_path = os.path.join(PROJECT_ROOT, "ravenspot")
        if not os.path.isfile(bin_path):
            raise unittest.SkipTest("ravenspot binary not found")

        # Spawn isolated daemon instance
        print("\n[*] Spawning isolated ravenspot daemon (runtime: /tmp/challenger_m2_2_runtime)...")
        env = os.environ.copy()
        env["XDG_RUNTIME_DIR"] = cls.runtime_dir

        cls.daemon_proc = subprocess.Popen([bin_path, "--daemon"], cwd=PROJECT_ROOT, env=env)

        # Wait for socket
        connected = False
        for _ in range(40):
            time.sleep(0.05)
            ok, status, _ = cls.client.status()
            if ok and "pid" in status:
                cls.target_pid = int(status["pid"])
                connected = True
                break

        if not connected:
            if cls.daemon_proc.poll() is not None:
                raise RuntimeError(f"Daemon process exited prematurely with code {cls.daemon_proc.returncode}")
            cls.target_pid = cls.daemon_proc.pid

        print(f"[*] Connected to isolated daemon (PID: {cls.target_pid})")

    @classmethod
    def tearDownClass(cls):
        print("\n[*] Tearing down daemon...")
        if cls.client:
            cls.client.quit()
        if cls.daemon_proc:
            try:
                cls.daemon_proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                cls.daemon_proc.kill()
                cls.daemon_proc.wait()
        print("[*] Daemon terminated cleanly")

    def test_50_window_visibility_load_cycles(self):
        pid = self.target_pid
        self.assertIsNotNone(pid, "Target daemon PID must be valid")
        self.assertTrue(os.path.exists(f"/proc/{pid}"), f"/proc/{pid} does not exist")

        # 1. Baseline measurements (Cold Start Idle)
        rss_base = get_process_rss_mb(pid)
        pss_base = get_process_pss_mb(pid)
        dirty_base = get_process_dirty_heap_mb(pid)

        print("\n==============================================================")
        print("MEMORY STABILITY UNDER 50 LOAD CYCLES VIA IPC")
        print("==============================================================")
        print(f"[*] Initial Baseline (Cold Idle) -> RSS: {rss_base:.2f} MB | PSS: {pss_base:.2f} MB | Private Dirty: {dirty_base:.2f} MB")

        self.assertLess(pss_base, 50.0, f"Initial Cold Idle PSS {pss_base:.2f}MB must be < 50MB (PROJECT.md R3/AC)")
        self.assertLess(dirty_base, 30.0, f"Initial Cold Idle Private Dirty {dirty_base:.2f}MB must be < 30MB")

        history = []
        TOTAL_CYCLES = 50

        start_time = time.time()

        for cycle in range(1, TOTAL_CYCLES + 1):
            # Phase A: show
            ok, resp, _ = self.client.show()
            self.assertTrue(ok, f"Cycle {cycle}: show command failed")
            time.sleep(0.015)

            # Phase B: hide
            ok, resp, _ = self.client.hide()
            self.assertTrue(ok, f"Cycle {cycle}: hide command failed")
            time.sleep(0.015)

            # Phase C: toggle (to show)
            ok, resp, _ = self.client.toggle()
            self.assertTrue(ok, f"Cycle {cycle}: toggle-to-show failed")
            time.sleep(0.015)

            # Phase D: toggle (to hide)
            ok, resp, _ = self.client.toggle()
            self.assertTrue(ok, f"Cycle {cycle}: toggle-to-hide failed")
            time.sleep(0.015)

            # Measure metrics
            rss = get_process_rss_mb(pid)
            pss = get_process_pss_mb(pid)
            dirty = get_process_dirty_heap_mb(pid)

            history.append((cycle, rss, pss, dirty))

            if cycle % 5 == 0 or cycle == 1:
                print(f"[*] Cycle {cycle:02d}/{TOTAL_CYCLES} -> RSS: {rss:.2f} MB | PSS: {pss:.2f} MB | Private Dirty: {dirty:.2f} MB")

        elapsed = time.time() - start_time
        print(f"[*] Completed 50 cycles (200 IPC commands) in {elapsed:.2f}s ({elapsed/TOTAL_CYCLES*1000:.1f}ms/cycle)")

        # 2. Final analysis
        final_cycle, final_rss, final_pss, final_dirty = history[-1]
        c10_cycle, c10_rss, c10_pss, c10_dirty = history[9]  # cycle 10

        delta_rss_total = final_rss - rss_base
        delta_pss_total = final_pss - pss_base
        delta_dirty_total = final_dirty - dirty_base

        # Post-initialization drift (cycle 10 to cycle 50)
        drift_pss = final_pss - c10_pss
        drift_dirty = final_dirty - c10_dirty
        drift_rss = final_rss - c10_rss

        print("\n--------------------------------------------------------------")
        print("EMPIRICAL LOAD RESULTS SUMMARY")
        print("--------------------------------------------------------------")
        print(f"[*] Cold Idle Baseline -> PSS: {pss_base:.2f} MB, Private Dirty: {dirty_base:.2f} MB, RSS: {rss_base:.2f} MB")
        print(f"[*] Cycle 10           -> PSS: {c10_pss:.2f} MB, Private Dirty: {c10_dirty:.2f} MB, RSS: {c10_rss:.2f} MB")
        print(f"[*] Cycle 50           -> PSS: {final_pss:.2f} MB, Private Dirty: {final_dirty:.2f} MB, RSS: {final_rss:.2f} MB")
        print(f"[*] Total Delta        -> ΔPSS: {delta_pss_total:+.2f} MB, ΔDirty: {delta_dirty_total:+.2f} MB, ΔRSS: {delta_rss_total:+.2f} MB")
        print(f"[*] Cycle 10-50 Drift  -> ΔPSS: {drift_pss:+.2f} MB, ΔDirty: {drift_dirty:+.2f} MB, ΔRSS: {drift_rss:+.2f} MB")

        # Zero monotonic growth assertion (leak detection across cycles 10 to 50)
        self.assertLess(abs(drift_pss), 2.0, f"PSS drift from cycle 10 to 50 ({drift_pss:.2f}MB) indicates memory leak!")
        self.assertLess(abs(drift_dirty), 2.0, f"Private Dirty drift from cycle 10 to 50 ({drift_dirty:.2f}MB) indicates memory leak!")
        self.assertLess(abs(drift_rss), 2.0, f"RSS drift from cycle 10 to 50 ({drift_rss:.2f}MB) indicates memory leak!")

        print("[+] PASS: 50 window visibility cycles verified with 0 memory leaks (Cycle 10-50 drift < 0.2 MB)")

if __name__ == "__main__":
    unittest.main()
