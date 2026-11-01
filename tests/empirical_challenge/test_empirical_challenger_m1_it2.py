#!/usr/bin/env python3
"""
Empirical Challenger Test Suite for Milestone 1 Iteration 2
Author: Empirical Challenger 1 (challenger_m1_it2_1)
Target: Native compiled binary /home/dokuro/Masaüstü/omarchyapp/ravenspot

Test Dimensions:
1. Socket Inactivity Timeout (2.0s deadline, "error: connection timed out\n", EOF disconnect, zero orphaned FDs).
2. High-concurrency burst toggles (100 rapid requests, zero errors, avg latency < 0.5ms).
3. Crash Recovery (kill -9 SIGKILL, restart, instant lock acquisition and socket rebind).
"""

import concurrent.futures
import json
import os
import signal
import socket
import subprocess
import sys
import time
from typing import Dict, List, Optional, Set, Tuple

BINARY_PATH = "/home/dokuro/Masaüstü/omarchyapp/ravenspot"
RUNTIME_DIR = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
SOCKET_PATH = os.path.join(RUNTIME_DIR, "ravenspot.sock")
LOCK_PATH = os.path.join(RUNTIME_DIR, "ravenspot.lock")


def log(msg: str):
    print(f"[CHALLENGER-IT2] {msg}", flush=True)


def get_process_fds(pid: int) -> Set[str]:
    """Returns set of file descriptor targets for the given process."""
    fd_dir = f"/proc/{pid}/fd"
    targets = set()
    if not os.path.exists(fd_dir):
        return targets
    try:
        for entry in os.listdir(fd_dir):
            entry_path = os.path.join(fd_dir, entry)
            try:
                target = os.readlink(entry_path)
                targets.add(f"{entry}->{target}")
            except OSError:
                pass
    except OSError:
        pass
    return targets


def send_ipc_cmd(sock_path: str, cmd: str, timeout: float = 2.0) -> Tuple[bool, str, float]:
    """Send newline-terminated command and return (ok, response, latency_ms)."""
    if not cmd.endswith("\n"):
        cmd += "\n"
    start = time.perf_counter_ns()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(sock_path)
        s.sendall(cmd.encode("utf-8"))
        chunks = []
        while True:
            buf = s.recv(4096)
            if not buf:
                break
            chunks.append(buf)
            if b"\n" in buf:
                break
        elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000.0
        return True, b"".join(chunks).decode("utf-8", errors="replace").strip(), elapsed_ms
    except Exception as e:
        elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000.0
        return False, str(e), elapsed_ms
    finally:
        try:
            s.close()
        except Exception:
            pass


class DaemonManager:
    def __init__(self, binary_path: str = BINARY_PATH):
        self.binary_path = binary_path
        self.proc: Optional[subprocess.Popen] = None

    def clean_stale(self):
        subprocess.run(["pkill", "-9", "-f", "ravenspot"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.1)
        for p in [SOCKET_PATH, LOCK_PATH]:
            if os.path.exists(p):
                try:
                    os.unlink(p)
                except OSError:
                    pass

    def start(self, max_wait_sec: float = 2.0) -> int:
        self.proc = subprocess.Popen(
            [self.binary_path, "--daemon"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        
        # Poll until socket is bound and ready to accept connections
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < max_wait_sec:
            if self.proc.poll() is not None:
                raise RuntimeError(f"Daemon terminated prematurely with exit code {self.proc.returncode}")
            if os.path.exists(SOCKET_PATH):
                try:
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    s.settimeout(0.1)
                    s.connect(SOCKET_PATH)
                    s.sendall(b"status\n")
                    resp = s.recv(1024)
                    s.close()
                    if b"status" in resp:
                        return self.proc.pid
                except Exception:
                    pass
            time.sleep(0.02)
            
        raise RuntimeError("Timed out waiting for daemon socket to become ready.")

    def stop(self, wait_sec: float = 0.5):
        if self.proc and self.proc.poll() is None:
            try:
                send_ipc_cmd(SOCKET_PATH, "quit", timeout=0.5)
                self.proc.wait(timeout=wait_sec)
            except Exception:
                try:
                    self.proc.terminate()
                    self.proc.wait(timeout=wait_sec)
                except Exception:
                    self.proc.kill()
                    self.proc.wait()
        self.proc = None

    def kill_sigkill(self):
        if self.proc and self.proc.poll() is None:
            os.kill(self.proc.pid, signal.SIGKILL)
            self.proc.wait()


def test_1_inactivity_timeout() -> bool:
    log("=================================================================")
    log("TEST 1: Socket Inactivity Timeout (2.0s deadline & FD audit)")
    log("=================================================================")
    daemon = DaemonManager()
    daemon.clean_stale()

    try:
        pid = daemon.start()
        log(f"Daemon started with PID {pid}")
        time.sleep(0.2) # Allow initial thread/socket stabilization

        # Baseline FDs
        initial_fds = get_process_fds(pid)
        log(f"Initial baseline FD count: {len(initial_fds)}")

        # 1.1 Pure Idle Client (Connect and send 0 bytes)
        log("Subtest 1.1: Connecting idle client (0 bytes sent)...")
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        t_connect = time.perf_counter()
        s.connect(SOCKET_PATH)

        time.sleep(0.1)
        connected_fds = get_process_fds(pid)
        new_fds = connected_fds - initial_fds
        log(f"Attached client FDs during idle wait: {len(new_fds)} -> {list(new_fds)}")
        if len(new_fds) == 0:
            log("FAIL: Daemon did not register client socket FD!")
            s.close()
            return False

        # Read timeout message
        s.settimeout(3.0)
        chunks = []
        try:
            while True:
                data = s.recv(4096)
                if not data:
                    break
                chunks.append(data)
        except socket.timeout:
            log("FAIL: Client socket timed out before server sent timeout message!")
            s.close()
            return False

        elapsed_sec = time.perf_counter() - t_connect
        s.close()
        full_response = b"".join(chunks).decode("utf-8", errors="replace")
        log(f"Received response in {elapsed_sec:.3f}s: {repr(full_response)}")

        expected_msg = "error: connection timed out\n"
        if full_response != expected_msg:
            log(f"FAIL: Expected {repr(expected_msg)}, got {repr(full_response)}")
            return False

        # Verify timeout duration: default is 2000ms (acceptable timer resolution: 1.85s - 2.25s)
        if not (1.85 <= elapsed_sec <= 2.25):
            log(f"FAIL: Timeout duration {elapsed_sec:.3f}s is outside acceptable 2.0s window [1.85s, 2.25s]!")
            return False

        # Verify zero orphaned FDs
        time.sleep(0.3)
        post_fds = get_process_fds(pid)
        orphaned = post_fds - initial_fds
        log(f"Post-timeout FD count: {len(post_fds)} (Orphaned: {len(orphaned)})")
        if len(orphaned) > 0:
            log(f"FAIL: Orphaned FDs detected: {orphaned}")
            return False
        log("PASS: Subtest 1.1 (Idle client timed out cleanly at 2.0s, 0 orphaned FDs)")

        # 1.2 Partial Command Client ("tog" sent, no newline)
        log("Subtest 1.2: Connecting client with partial command ('tog' without newline)...")
        s2 = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        t_connect2 = time.perf_counter()
        s2.connect(SOCKET_PATH)
        s2.sendall(b"tog")

        s2.settimeout(3.0)
        chunks2 = []
        try:
            while True:
                data = s2.recv(4096)
                if not data:
                    break
                chunks2.append(data)
        except socket.timeout:
            log("FAIL: Client socket timed out before server sent timeout message!")
            s2.close()
            return False

        elapsed_sec2 = time.perf_counter() - t_connect2
        s2.close()
        full_response2 = b"".join(chunks2).decode("utf-8", errors="replace")
        log(f"Received partial-cmd response in {elapsed_sec2:.3f}s: {repr(full_response2)}")

        if full_response2 != expected_msg:
            log(f"FAIL: Expected {repr(expected_msg)}, got {repr(full_response2)}")
            return False

        if not (1.85 <= elapsed_sec2 <= 2.25):
            log(f"FAIL: Partial command timeout duration {elapsed_sec2:.3f}s outside [1.85s, 2.25s]!")
            return False

        time.sleep(0.3)
        post_fds2 = get_process_fds(pid)
        orphaned2 = post_fds2 - initial_fds
        if len(orphaned2) > 0:
            log(f"FAIL: Orphaned FDs detected after partial command: {orphaned2}")
            return False
        log("PASS: Subtest 1.2 (Partial command client timed out cleanly at 2.0s, 0 orphaned FDs)")

        # 1.3 Burst of 10 Concurrent Idle Connections
        log("Subtest 1.3: 10 concurrent idle clients connecting simultaneously...")
        sockets = [socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) for _ in range(10)]
        t_start3 = time.perf_counter()
        for sock in sockets:
            sock.connect(SOCKET_PATH)

        time.sleep(0.1)
        multi_connected_fds = get_process_fds(pid)
        log(f"Active FDs with 10 idle clients: {len(multi_connected_fds)} (+{len(multi_connected_fds - initial_fds)})")

        responses = []
        for sock in sockets:
            sock.settimeout(3.0)
            c = []
            try:
                while True:
                    d = sock.recv(4096)
                    if not d:
                        break
                    c.append(d)
            except Exception as e:
                c.append(str(e).encode())
            sock.close()
            responses.append(b"".join(c).decode("utf-8", errors="replace"))

        elapsed_sec3 = time.perf_counter() - t_start3
        log(f"All 10 idle connections completed in {elapsed_sec3:.3f}s")
        if not all(r == expected_msg for r in responses):
            log(f"FAIL: Not all 10 clients received expected timeout error! Responses: {responses}")
            return False

        time.sleep(0.3)
        final_fds = get_process_fds(pid)
        final_orphaned = final_fds - initial_fds
        log(f"Final FD count after 10 concurrent timeouts: {len(final_fds)} (Orphaned: {len(final_orphaned)})")
        if len(final_orphaned) > 0:
            log(f"FAIL: Orphaned FDs after 10 concurrent timeouts: {final_orphaned}")
            return False

        log("PASS: Subtest 1.3 (10 concurrent idle connections timed out cleanly, 0 orphaned FDs)")
        return True

    finally:
        daemon.stop()


def test_2_burst_toggles() -> bool:
    log("=================================================================")
    log("TEST 2: High-Concurrency Burst Toggles (100 rapid requests)")
    log("=================================================================")
    daemon = DaemonManager()
    daemon.clean_stale()

    try:
        pid = daemon.start()
        log(f"Daemon started with PID {pid}")

        # Warm-up show/hide cycle
        send_ipc_cmd(SOCKET_PATH, "toggle")
        time.sleep(0.05)
        send_ipc_cmd(SOCKET_PATH, "toggle")
        time.sleep(0.05)

        # 2.1 100 Paced Burst Toggles (10ms pacing per Wayland compositor frame)
        log("Subtest 2.1: 100 rapid toggle requests with 10ms frame pacing...")
        latencies_paced = []
        errors_paced = 0
        for _ in range(100):
            ok, resp, lat = send_ipc_cmd(SOCKET_PATH, "toggle")
            if not ok or resp != "ok":
                errors_paced += 1
            latencies_paced.append(lat)
            time.sleep(0.01)

        avg_lat_paced = sum(latencies_paced) / len(latencies_paced)
        min_lat_paced = min(latencies_paced)
        max_lat_paced = max(latencies_paced)
        p95_lat_paced = sorted(latencies_paced)[95]
        p99_lat_paced = sorted(latencies_paced)[99]

        log(f"Subtest 2.1 Results: Errors={errors_paced}, Avg={avg_lat_paced:.4f}ms, Min={min_lat_paced:.4f}ms, Max={max_lat_paced:.4f}ms, P95={p95_lat_paced:.4f}ms, P99={p99_lat_paced:.4f}ms")

        # 2.2 100 Concurrent Burst Requests (100 threads sending status/toggle queries)
        log("Subtest 2.2: 100 concurrent burst queries across 20 worker threads...")
        latencies_conc = []
        errors_conc = 0

        def client_worker(idx: int):
            cmd = "toggle" if (idx % 2 == 0) else "status"
            return send_ipc_cmd(SOCKET_PATH, cmd)

        t_conc_start = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
            futures = [executor.submit(client_worker, i) for i in range(100)]
            for f in concurrent.futures.as_completed(futures):
                ok, resp, lat = f.result()
                if not ok or (resp != "ok" and "status" not in resp):
                    errors_conc += 1
                latencies_conc.append(lat)

        total_conc_duration_ms = (time.perf_counter() - t_conc_start) * 1000.0
        avg_lat_conc = sum(latencies_conc) / len(latencies_conc)
        p95_lat_conc = sorted(latencies_conc)[95]

        log(f"Subtest 2.2 Results: Errors={errors_conc}, TotalTime={total_conc_duration_ms:.2f}ms, Avg={avg_lat_conc:.4f}ms, P95={p95_lat_conc:.4f}ms")

        # 2.3 Pure IPC Burst (100 rapid status requests with zero pacing)
        log("Subtest 2.3: 100 unpaced pure IPC rapid queries...")
        latencies_pure = []
        errors_pure = 0
        for _ in range(100):
            ok, resp, lat = send_ipc_cmd(SOCKET_PATH, "status")
            if not ok or "status" not in resp:
                errors_pure += 1
            latencies_pure.append(lat)

        avg_lat_pure = sum(latencies_pure) / len(latencies_pure)
        log(f"Subtest 2.3 Results: Errors={errors_pure}, Avg={avg_lat_pure:.4f}ms, Max={max(latencies_pure):.4f}ms, Min={min(latencies_pure):.4f}ms")

        passed = True
        if errors_paced > 0 or errors_conc > 0 or errors_pure > 0:
            log(f"FAIL: Errors detected in burst tests! (paced={errors_paced}, conc={errors_conc}, pure={errors_pure})")
            passed = False

        if avg_lat_paced >= 0.5:
            log(f"FAIL: Paced toggle average latency {avg_lat_paced:.4f}ms exceeded 0.5ms threshold!")
            passed = False

        if passed:
            log(f"PASS: Test 2 (100 rapid requests completed with 0 errors and avg latency {avg_lat_paced:.4f}ms < 0.5ms)")
        return passed

    finally:
        daemon.stop()


def test_3_crash_recovery() -> bool:
    log("=================================================================")
    log("TEST 3: Crash Recovery (kill -9 SIGKILL, restart, lock & socket rebind)")
    log("=================================================================")
    daemon = DaemonManager()
    daemon.clean_stale()

    try:
        pid1 = daemon.start()
        log(f"Initial daemon running with PID {pid1}")

        ok, resp, _ = send_ipc_cmd(SOCKET_PATH, "status")
        if not ok or str(pid1) not in resp:
            log(f"FAIL: Initial daemon did not respond to status probe: {resp}")
            return False

        if not os.path.exists(SOCKET_PATH) or not os.path.exists(LOCK_PATH):
            log("FAIL: Socket or lock file missing during normal execution!")
            return False

        log("Sending SIGKILL (kill -9) to daemon process...")
        daemon.kill_sigkill()

        # Check post-kill filesystem state
        sock_exists = os.path.exists(SOCKET_PATH)
        lock_exists = os.path.exists(LOCK_PATH)
        log(f"Post-SIGKILL filesystem state: socket={sock_exists}, lock={lock_exists}")
        if not sock_exists or not lock_exists:
            log("WARNING: Expected stale socket and lock files to persist after abrupt SIGKILL!")

        # Restart daemon immediately
        log("Restarting daemon immediately (testing instant lock acquisition & socket rebind)...")
        t_restart = time.perf_counter()
        pid2 = daemon.start()
        restart_ms = (time.perf_counter() - t_restart) * 1000.0
        log(f"Resurrected daemon running with PID {pid2} (Restart & rebind completed in {restart_ms:.2f}ms)")

        if pid2 == pid1:
            log("FAIL: PID did not change after restart!")
            return False

        # Probe status
        ok_status, resp_status, lat_status = send_ipc_cmd(SOCKET_PATH, "status")
        log(f"Probed resurrected status in {lat_status:.3f}ms: {resp_status}")
        if not ok_status or str(pid2) not in resp_status:
            log(f"FAIL: Resurrected daemon failed status probe: {resp_status}")
            return False

        # Probe toggle
        ok_toggle, resp_toggle, lat_toggle = send_ipc_cmd(SOCKET_PATH, "toggle")
        log(f"Probed resurrected toggle in {lat_toggle:.3f}ms: {resp_toggle}")
        if not ok_toggle or resp_toggle != "ok":
            log(f"FAIL: Resurrected daemon failed toggle probe: {resp_toggle}")
            return False

        # Verify second instance rejection
        sub = subprocess.run(
            [BINARY_PATH, "--daemon"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=2.0
        )
        out = (sub.stdout + sub.stderr).strip()
        log(f"Second instance attempt while daemon is active: code={sub.returncode}, output='{out}'")
        if sub.returncode != 0 or "already running" not in out:
            log("FAIL: Second instance was not properly rejected by lock!")
            return False

        log("PASS: Test 3 (Crash recovery verified: instant lock acquisition, clean stale socket rebind, healthy IPC)")
        return True

    finally:
        daemon.stop()


def main():
    log("=================================================================")
    log("Milestone 1 Iteration 2 Empirical Challenge Harness")
    log(f"Binary: {BINARY_PATH}")
    log(f"Socket: {SOCKET_PATH}")
    log("=================================================================")

    res1 = test_1_inactivity_timeout()
    res2 = test_2_burst_toggles()
    res3 = test_3_crash_recovery()

    log("=================================================================")
    log("EMPIRICAL TEST SUITE SUMMARY")
    log("=================================================================")
    log(f"Test 1 (Inactivity Timeout & FD Audit):   {'PASS' if res1 else 'FAIL'}")
    log(f"Test 2 (High-Concurrency Burst Toggles): {'PASS' if res2 else 'FAIL'}")
    log(f"Test 3 (Crash Recovery & Stale Rebind):   {'PASS' if res3 else 'FAIL'}")
    log("=================================================================")

    if res1 and res2 and res3:
        log("FINAL VERDICT: ALL TESTS PASSED -> APPROVE")
        sys.exit(0)
    else:
        log("FINAL VERDICT: TESTS FAILED -> REQUEST_CHANGES")
        sys.exit(1)


if __name__ == "__main__":
    main()
