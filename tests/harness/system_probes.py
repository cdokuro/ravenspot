import os
import resource
import time
from typing import Tuple, Dict, Any, Optional

def get_process_rss_mb(pid: int) -> float:
    """Read VmRSS in MB from /proc/<pid>/status."""
    status_path = f"/proc/{pid}/status"
    if not os.path.exists(status_path):
        return 0.0
    try:
        with open(status_path, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    parts = line.split()
                    kb = float(parts[1])
                    return kb / 1024.0
    except Exception:
        pass
    return 0.0

def get_process_statm_mb(pid: int) -> float:
    """Read resident pages from /proc/<pid>/statm and convert to MB."""
    statm_path = f"/proc/{pid}/statm"
    if not os.path.exists(statm_path):
        return 0.0
    try:
        pagesize = resource.getpagesize()
        with open(statm_path, "r", encoding="utf-8") as f:
            parts = f.read().split()
            resident_pages = int(parts[1])
            return (resident_pages * pagesize) / (1024.0 * 1024.0)
    except Exception:
        pass
    return 0.0

def get_process_pss_mb(pid: int) -> float:
    """Read PSS (Proportional Set Size) in MB from /proc/<pid>/smaps_rollup if available."""
    smaps_path = f"/proc/{pid}/smaps_rollup"
    if os.path.exists(smaps_path):
        try:
            with open(smaps_path, "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("Pss:"):
                        parts = line.split()
                        return float(parts[1]) / 1024.0
        except Exception:
            pass
    # Fallback to VmRSS
    return get_process_rss_mb(pid)

def get_process_dirty_heap_mb(pid: int) -> float:
    """Read Private_Dirty in MB from /proc/<pid>/smaps_rollup."""
    smaps_path = f"/proc/{pid}/smaps_rollup"
    if os.path.exists(smaps_path):
        try:
            with open(smaps_path, "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("Private_Dirty:"):
                        parts = line.split()
                        return float(parts[1]) / 1024.0
        except Exception:
            pass
    return 0.0

def read_kernel_cpu_percent(sample_interval: float = 0.05) -> float:
    """Read two consecutive samples from /proc/stat to compute system CPU %."""
    def sample():
        with open("/proc/stat", "r", encoding="utf-8") as f:
            fields = f.readline().split()[1:]
            values = [float(x) for x in fields]
            idle = values[3] + values[4]
            total = sum(values)
            return idle, total

    try:
        idle1, total1 = sample()
        time.sleep(sample_interval)
        idle2, total2 = sample()

        idle_delta = idle2 - idle1
        total_delta = total2 - total1
        if total_delta <= 0:
            return 0.0
        cpu_usage = 100.0 * (1.0 - (idle_delta / total_delta))
        return max(0.0, min(100.0, cpu_usage))
    except Exception:
        return 0.0

def read_kernel_meminfo() -> Dict[str, float]:
    """Read /proc/meminfo to get RAM metrics."""
    res = {"total_mb": 0.0, "available_mb": 0.0, "used_percent": 0.0}
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            total_kb = 0.0
            avail_kb = 0.0
            for line in f:
                if line.startswith("MemTotal:"):
                    total_kb = float(line.split()[1])
                elif line.startswith("MemAvailable:"):
                    avail_kb = float(line.split()[1])
            if total_kb > 0:
                res["total_mb"] = total_kb / 1024.0
                res["available_mb"] = avail_kb / 1024.0
                used_kb = max(0.0, total_kb - avail_kb)
                res["used_percent"] = (used_kb / total_kb) * 100.0
    except Exception:
        pass
    return res

def read_kernel_uptime() -> float:
    """Read /proc/uptime to get total system uptime seconds."""
    try:
        with open("/proc/uptime", "r", encoding="utf-8") as f:
            return float(f.read().split()[0])
    except Exception:
        return 0.0

def measure_latency_ns(func, *args, **kwargs) -> Tuple[Any, int, float]:
    """Measure function execution time in nanoseconds and milliseconds."""
    start = time.perf_counter_ns()
    result = func(*args, **kwargs)
    end = time.perf_counter_ns()
    elapsed_ns = end - start
    elapsed_ms = elapsed_ns / 1_000_000.0
    return result, elapsed_ns, elapsed_ms
