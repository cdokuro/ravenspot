#!/usr/bin/env python3
"""
Independent Adversarial Socket Timeout & Resource Leak Probe
Author: Reviewer 2 & Adversarial Critic (reviewer_m1_it2_2)
"""

import os
import sys
import time
import socket
import struct
import subprocess
from typing import Set, List

BINARY_PATH = "/home/dokuro/Masaüstü/omarchyapp/ravenspot"
RUNTIME_DIR = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
SOCKET_PATH = os.path.join(RUNTIME_DIR, "ravenspot.sock")
LOCK_PATH = os.path.join(RUNTIME_DIR, "ravenspot.lock")

def log(msg: str):
    print(f"[ADVERSARIAL-PROBE-R2] {msg}", flush=True)

def get_process_fds(pid: int) -> Set[str]:
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

def cleanup_daemon():
    subprocess.run(["pkill", "-9", "-x", "ravenspot"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.15)
    for p in [SOCKET_PATH, LOCK_PATH]:
        if os.path.exists(p):
            try:
                os.unlink(p)
            except OSError:
                pass

def start_daemon() -> subprocess.Popen:
    cleanup_daemon()
    proc = subprocess.Popen(
        [BINARY_PATH, "--daemon"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(0.3)
    if proc.poll() is not None:
        stdout, stderr = proc.communicate()
        raise RuntimeError(f"Daemon failed to start: {stdout} {stderr}")
    return proc

def test_1_idle_timeout_and_50_connections():
    log("----------------------------------------------------------------------")
    log("TEST 1: 50 Concurrent Idle Connections - Inactivity Timeout & Zero FD Leak")
    log("----------------------------------------------------------------------")
    proc = start_daemon()
    pid = proc.pid
    try:
        initial_fds = get_process_fds(pid)
        log(f"Baseline daemon FD count: {len(initial_fds)}")

        CONCURRENT_CLIENTS = 50
        log(f"Spawning {CONCURRENT_CLIENTS} concurrent idle connections...")
        socks = []
        for i in range(CONCURRENT_CLIENTS):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCKET_PATH)
            socks.append(s)

        time.sleep(0.2)
        connected_fds = get_process_fds(pid)
        added_fds = len(connected_fds - initial_fds)
        log(f"Daemon FDs under 50 connections: {len(connected_fds)} (+{added_fds})")
        assert added_fds >= CONCURRENT_CLIENTS, f"Expected at least {CONCURRENT_CLIENTS} client FDs, got {added_fds}"

        log("Waiting 2.1s for server-side inactivity timeout (2000ms)...")
        start_t = time.perf_counter()
        responses = []
        for s in socks:
            s.settimeout(3.0)
            chunks = []
            try:
                while True:
                    data = s.recv(4096)
                    if not data:
                        break
                    chunks.append(data)
            except socket.timeout:
                chunks.append(b"CLIENT_TIMEOUT_WAITING_FOR_SERVER")
            s.close()
            responses.append(b"".join(chunks).decode("utf-8", errors="replace"))

        elapsed = time.perf_counter() - start_t
        log(f"All 50 sockets completed in {elapsed:.3f}s")

        expected_msg = "error: connection timed out\n"
        matched = sum(1 for r in responses if r == expected_msg)
        log(f"Received expected timeout message on {matched}/{CONCURRENT_CLIENTS} sockets")
        assert matched == CONCURRENT_CLIENTS, f"Only {matched}/{CONCURRENT_CLIENTS} received timeout message!"

        time.sleep(0.3)
        final_fds = get_process_fds(pid)
        orphaned = final_fds - initial_fds
        log(f"Post-timeout daemon FD count: {len(final_fds)} (Orphaned: {len(orphaned)})")
        assert len(orphaned) == 0, f"Leaked file descriptors detected: {orphaned}"
        log("[PASS] TEST 1: 50 concurrent idle clients cleanly timed out with 0 FD leaks.")
    finally:
        cleanup_daemon()

def test_2_sliding_window_timeout():
    log("----------------------------------------------------------------------")
    log("TEST 2: Sliding Inactivity Window (Slowloris Drip vs Stalled Connection)")
    log("----------------------------------------------------------------------")
    proc = start_daemon()
    try:
        # Part A: Drip transmission under 2.0s interval -> Should succeed
        log("Part A: Dripping 't' ... 'o' ... 'g' ... 'g' ... 'l' ... 'e' ... '\\n' at 600ms intervals")
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(SOCKET_PATH)
        chars = ["t", "o", "g", "g", "l", "e", "\n"]
        for c in chars:
            s.sendall(c.encode())
            time.sleep(0.6)  # 600ms < 2000ms, should refresh timer

        resp = s.recv(1024).decode()
        s.close()
        log(f"Drip command response: {repr(resp)}")
        assert resp == "ok\n", f"Expected 'ok\\n', got {repr(resp)}"
        log("[PASS] Part A: Sliding timer correctly refreshed on partial command data.")

        # Part B: Drip with a 2.2s gap -> Should timeout mid-way
        log("Part B: Sending 'tog', then stalling for 2.2s...")
        s2 = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s2.connect(SOCKET_PATH)
        s2.sendall(b"tog")

        start_t = time.perf_counter()
        resp2 = s2.recv(1024).decode()
        elapsed = time.perf_counter() - start_t
        s2.close()
        log(f"Stalled client response in {elapsed:.3f}s: {repr(resp2)}")
        assert resp2 == "error: connection timed out\n", f"Expected timeout error, got {repr(resp2)}"
        assert 1.85 <= elapsed <= 2.25, f"Timeout duration {elapsed:.3f}s outside [1.85s, 2.25s]"
        log("[PASS] Part B: Stalled client timed out exactly at 2000ms.")
    finally:
        cleanup_daemon()

def test_3_early_client_disconnect_and_rst():
    log("----------------------------------------------------------------------")
    log("TEST 3: Early Client Disconnect & TCP/UNIX RST Simulation")
    log("----------------------------------------------------------------------")
    proc = start_daemon()
    pid = proc.pid
    try:
        initial_fds = get_process_fds(pid)

        # Connect and immediately send RST / linger 0 close
        for _ in range(20):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCKET_PATH)
            # Set SO_LINGER to 0 (hard abort)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            s.close()

        time.sleep(0.3)
        post_fds = get_process_fds(pid)
        orphaned = post_fds - initial_fds
        log(f"FDs after 20 abrupt RST closes: {len(post_fds)} (Orphaned: {len(orphaned)})")
        assert len(orphaned) == 0, f"Leaked FDs after RST: {orphaned}"

        # Verify daemon is fully healthy
        s_verify = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s_verify.connect(SOCKET_PATH)
        s_verify.sendall(b"status\n")
        reply = s_verify.recv(1024).decode()
        s_verify.close()
        log(f"Health verification reply: {repr(reply)}")
        assert '"status":"ok"' in reply, "Daemon became unhealthy after abrupt closes!"
        log("[PASS] TEST 3: Daemon handles abrupt client RST closes without leaks or crash.")
    finally:
        cleanup_daemon()

def test_4_daemon_shutdown_with_connected_clients():
    log("----------------------------------------------------------------------")
    log("TEST 4: Clean Daemon Shutdown (stop()) with Active Idle Clients")
    log("----------------------------------------------------------------------")
    proc = start_daemon()
    try:
        # Connect 5 idle clients
        idle_socks = []
        for _ in range(5):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCKET_PATH)
            idle_socks.append(s)

        time.sleep(0.1)
        # Send quit command via a 6th socket
        s_quit = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s_quit.connect(SOCKET_PATH)
        s_quit.sendall(b"quit\n")
        quit_resp = s_quit.recv(1024).decode()
        s_quit.close()
        log(f"Quit command response: {repr(quit_resp)}")
        assert quit_resp == "ok\n"

        # Wait for daemon process to exit
        exit_code = proc.wait(timeout=2.0)
        log(f"Daemon process terminated with exit code: {exit_code}")
        assert exit_code == 0, f"Daemon exited with non-zero code {exit_code}"

        # Verify that all idle sockets received EOF from server abort
        for idx, s in enumerate(idle_socks):
            remaining = s.recv(1024)
            s.close()
            assert len(remaining) == 0, f"Socket {idx} did not receive clean EOF on daemon stop"

        # Verify socket and lock files were cleaned up
        assert not os.path.exists(SOCKET_PATH), "Socket file was not unlinked on stop!"
        assert not os.path.exists(LOCK_PATH), "Lock file was not unlinked on stop!"
        log("[PASS] TEST 4: Daemon shutdown cleanly closed all active clients and cleaned up files.")
    finally:
        cleanup_daemon()

def main():
    log("Starting Reviewer 2 Independent Adversarial Socket Timeout Probe...")
    test_1_idle_timeout_and_50_connections()
    test_2_sliding_window_timeout()
    test_3_early_client_disconnect_and_rst()
    test_4_daemon_shutdown_with_connected_clients()
    log("======================================================================")
    log("ALL 4 ADVERSARIAL SOCKET TIMEOUT & FD LEAK PROBES PASSED PERFECTLY!")
    log("======================================================================")

if __name__ == "__main__":
    main()
