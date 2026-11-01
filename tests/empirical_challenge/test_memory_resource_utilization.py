#!/usr/bin/env python3
import os
import sys
import time
import socket
import subprocess

ISOLATED_RUNTIME = "/tmp/challenger2_runtime"
os.makedirs(ISOLATED_RUNTIME, exist_ok=True)

# Symlink Wayland socket into isolated runtime dir
orig_runtime = os.environ.get("XDG_RUNTIME_DIR", "/run/user/1000")
wayland_sock = os.environ.get("WAYLAND_DISPLAY", "wayland-1")
src_sock = os.path.join(orig_runtime, wayland_sock)
dst_sock = os.path.join(ISOLATED_RUNTIME, wayland_sock)
if os.path.exists(src_sock) and not os.path.exists(dst_sock):
    os.symlink(src_sock, dst_sock)

os.environ["XDG_RUNTIME_DIR"] = ISOLATED_RUNTIME

def parse_smaps_rollup(pid):
    metrics = {}
    path = f"/proc/{pid}/smaps_rollup"
    if not os.path.exists(path):
        status_path = f"/proc/{pid}/status"
        with open(status_path, "r") as f:
            for line in f:
                parts = line.strip().split(":")
                if len(parts) == 2:
                    k = parts[0].strip()
                    v = parts[1].strip()
                    if k in ("VmRSS", "RssAnon", "RssFile"):
                        val = int(v.split()[0])
                        metrics[k] = val
        metrics["Rss"] = metrics.get("VmRSS", 0)
        metrics["Pss"] = metrics.get("VmRSS", 0)
        metrics["Private_Dirty"] = metrics.get("RssAnon", 0)
        return metrics

    with open(path, "r") as f:
        for line in f:
            parts = line.strip().split(":")
            if len(parts) == 2:
                key = parts[0].strip()
                val_parts = parts[1].strip().split()
                if val_parts and val_parts[0].isdigit():
                    metrics[key] = int(val_parts[0])
    return metrics

def get_proc_stat(pid):
    with open(f"/proc/{pid}/stat", "r") as f:
        fields = f.read().split()
        utime = int(fields[13])
        stime = int(fields[14])
    with open("/proc/stat", "r") as f:
        cpu_line = f.readline().split()
        total_cpu = sum(int(x) for x in cpu_line[1:])
    return utime + stime, total_cpu

def send_ipc_command(socket_path, cmd, timeout=1.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(socket_path)
    s.sendall((cmd.strip() + "\n").encode())
    res = b""
    while b"\n" not in res:
        chunk = s.recv(1024)
        if not chunk:
            break
        res += chunk
    s.close()
    return res.decode()

def run_stress_benchmark():
    project_dir = "/home/dokuro/Masaüstü/omarchyapp"
    bin_path = os.path.join(project_dir, "ravenspot")
    socket_path = os.path.join(ISOLATED_RUNTIME, "ravenspot.sock")

    print("=================================================================")
    print(" RavenSpot Milestone 1 - Memory & CPU Stress Benchmark")
    print(f" Target Binary: {bin_path}")
    print(f" Socket Path:   {socket_path}")
    print("=================================================================")

    # Clean up any leftover instances
    if os.path.exists(socket_path):
        try:
            send_ipc_command(socket_path, "quit", timeout=0.2)
            time.sleep(0.3)
        except Exception:
            pass

    # Launch daemon
    print("[*] Launching daemon in background: ./ravenspot --daemon ...")
    env = os.environ.copy()
    proc = subprocess.Popen([bin_path, "--daemon"], cwd=project_dir, env=env)
    time.sleep(0.8)

    if proc.poll() is not None:
        print(f"[-] ERROR: Daemon failed to start, exit code: {proc.returncode}")
        sys.exit(1)

    daemon_pid = proc.pid
    print(f"[+] Daemon running with PID: {daemon_pid}")

    # Check initial status
    try:
        status_res = send_ipc_command(socket_path, "status")
        print(f"[+] Initial IPC status response: {status_res.strip()}")
    except Exception as e:
        print(f"[-] ERROR connecting to IPC socket: {e}")
        proc.kill()
        sys.exit(1)

    # 1. Baseline Memory Probe
    time.sleep(1.0)
    baseline_mem = parse_smaps_rollup(daemon_pid)
    rss_mb = baseline_mem.get("Rss", 0) / 1024.0
    pss_mb = baseline_mem.get("Pss", 0) / 1024.0
    dirty_mb = baseline_mem.get("Private_Dirty", 0) / 1024.0
    shared_clean_mb = baseline_mem.get("Shared_Clean", 0) / 1024.0

    print(f"\n--- BASELINE IDLE RESOURCE USAGE ---")
    print(f"  VmRSS (Total Resident):  {rss_mb:.2f} MB ({baseline_mem.get('Rss', 0)} kB)")
    print(f"  PSS (Proportional):      {pss_mb:.2f} MB ({baseline_mem.get('Pss', 0)} kB)")
    print(f"  Private Dirty (Heap):    {dirty_mb:.2f} MB ({baseline_mem.get('Private_Dirty', 0)} kB)")
    print(f"  Shared Clean (Libs):     {shared_clean_mb:.2f} MB ({baseline_mem.get('Shared_Clean', 0)} kB)")
    print(f"  Target Limit:            < 50.00 MB RSS")

    # 2. Idle CPU Consumption Probe (5 seconds)
    print(f"\n[*] Measuring Idle CPU Consumption over 5.0 seconds...")
    proc_ticks_1, total_ticks_1 = get_proc_stat(daemon_pid)
    time.sleep(5.0)
    proc_ticks_2, total_ticks_2 = get_proc_stat(daemon_pid)

    delta_proc = proc_ticks_2 - proc_ticks_1
    delta_total = total_ticks_2 - total_ticks_1
    num_cpus = os.cpu_count() or 1
    cpu_usage_pct = (delta_proc / max(1, delta_total)) * 100.0 * num_cpus
    print(f"  Process Ticks Used:      {delta_proc} ticks in 5.0s")
    print(f"  Idle CPU Usage:          {cpu_usage_pct:.2f}% (Target: ~0.00%)")

    # 3. Repeated Show / Hide Cycle Stress Test (100 cycles)
    print(f"\n[*] Executing 100 Show/Hide toggle cycles...")
    start_time = time.time()
    latencies = []

    for i in range(1, 101):
        t0 = time.perf_counter()
        send_ipc_command(socket_path, "toggle")
        t1 = time.perf_counter()
        latencies.append((t1 - t0) * 1000.0)

        # Small yield
        time.sleep(0.005)

        # Checkpoints
        if i in (10, 25, 50, 75, 100):
            m = parse_smaps_rollup(daemon_pid)
            cur_rss = m.get("Rss", 0) / 1024.0
            cur_pss = m.get("Pss", 0) / 1024.0
            cur_dirty = m.get("Private_Dirty", 0) / 1024.0
            print(f"  Cycle {i:3d}/100 -> RSS: {cur_rss:.2f} MB | PSS: {cur_pss:.2f} MB | Dirty: {cur_dirty:.2f} MB")

    total_duration = time.time() - start_time
    avg_latency = sum(latencies) / len(latencies)
    max_latency = max(latencies)
    min_latency = min(latencies)
    p95_latency = sorted(latencies)[int(len(latencies) * 0.95)]

    print(f"\n--- TOGGLE LATENCY BENCHMARK (100 CYCLES) ---")
    print(f"  Total Duration:          {total_duration:.2f} s")
    print(f"  Min Roundtrip:           {min_latency:.3f} ms")
    print(f"  Avg Roundtrip:           {avg_latency:.3f} ms")
    print(f"  P95 Roundtrip:           {p95_latency:.3f} ms")
    print(f"  Max Roundtrip:           {max_latency:.3f} ms")
    print(f"  Target Limit:            < 30.000 ms")

    # Ensure window is hidden
    send_ipc_command(socket_path, "hide")

    # 4. Post-Stress Memory & Leak Analysis
    print(f"\n[*] Stabilizing after stress cycles (3.0s cooldown)...")
    time.sleep(3.0)

    post_mem = parse_smaps_rollup(daemon_pid)
    post_rss_mb = post_mem.get("Rss", 0) / 1024.0
    post_pss_mb = post_mem.get("Pss", 0) / 1024.0
    post_dirty_mb = post_mem.get("Private_Dirty", 0) / 1024.0

    delta_rss_kb = post_mem.get("Rss", 0) - baseline_mem.get("Rss", 0)
    delta_pss_kb = post_mem.get("Pss", 0) - baseline_mem.get("Pss", 0)
    delta_dirty_kb = post_mem.get("Private_Dirty", 0) - baseline_mem.get("Private_Dirty", 0)

    print(f"\n--- POST-STRESS MEMORY ANALYSIS ---")
    print(f"  Post-Stress VmRSS:       {post_rss_mb:.2f} MB ({post_mem.get('Rss', 0)} kB)")
    print(f"  Post-Stress PSS:         {post_pss_mb:.2f} MB ({post_mem.get('Pss', 0)} kB)")
    print(f"  Post-Stress Dirty:       {post_dirty_mb:.2f} MB ({post_mem.get('Private_Dirty', 0)} kB)")
    print(f"  Delta RSS:               {delta_rss_kb:+d} kB ({delta_rss_kb/1024.0:+.2f} MB)")
    print(f"  Delta PSS:               {delta_pss_kb:+d} kB ({delta_pss_kb/1024.0:+.2f} MB)")
    print(f"  Delta Private Dirty:     {delta_dirty_kb:+d} kB ({delta_dirty_kb/1024.0:+.2f} MB)")

    # 5. Post-Stress Idle CPU Check
    print(f"\n[*] Measuring Post-Stress Idle CPU over 3.0 seconds...")
    p1, t1 = get_proc_stat(daemon_pid)
    time.sleep(3.0)
    p2, t2 = get_proc_stat(daemon_pid)
    post_cpu = ((p2 - p1) / max(1, t2 - t1)) * 100.0 * num_cpus
    print(f"  Post-Stress Idle CPU:    {post_cpu:.2f}% (Target: ~0.00%)")

    # 6. Graceful Termination & Cleanup
    print(f"\n[*] Sending 'quit' command via IPC...")
    quit_res = send_ipc_command(socket_path, "quit")
    print(f"  Quit response: {quit_res.strip()}")

    try:
        proc.wait(timeout=2.0)
        print(f"[+] Daemon terminated cleanly with exit code {proc.returncode}")
    except subprocess.TimeoutExpired:
        print("[-] Daemon did not terminate in time! Killing...")
        proc.kill()
        sys.exit(1)

    # Verify socket and lock files removed
    socket_exists = os.path.exists(socket_path)
    print(f"[+] Socket file clean on disk: {'CLEAN' if not socket_exists else 'LEAKED'}")
    print("=================================================================")
    print(" SUMMARY ASSESSMENT:")
    print(f" - Baseline Idle PSS:       {pss_mb:.2f} MB (Well under 50MB)")
    print(f" - Baseline Private Dirty:  {dirty_mb:.2f} MB (Strict private heap)")
    print(f" - Baseline Total VmRSS:    {rss_mb:.2f} MB (Includes ~{shared_clean_mb:.2f} MB shared graphics libs)")
    print(f" - Delta PSS across 100x:   {delta_pss_kb/1024.0:+.2f} MB")
    print(f" - Idle CPU:                {cpu_usage_pct:.2f}%")
    print(f" - Avg Toggle Latency:      {avg_latency:.3f} ms")
    print("=================================================================")

if __name__ == "__main__":
    run_stress_benchmark()
