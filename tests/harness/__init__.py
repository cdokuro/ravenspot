"""
E2E Test Harness for RavenSpot Native Launcher.
Provides clients, probes, and simulated daemon fixtures for opaque-box testing.
"""

from .daemon_client import RavenSpotIPCClient
from .mock_daemon import MockRavenSpotDaemon
from .system_probes import (
    get_process_rss_mb,
    get_process_statm_mb,
    get_process_pss_mb,
    read_kernel_cpu_percent,
    read_kernel_meminfo,
    read_kernel_uptime,
    measure_latency_ns
)
from .dbus_probes import MockDBusPowerManagement

__all__ = [
    "RavenSpotIPCClient",
    "MockRavenSpotDaemon",
    "get_process_rss_mb",
    "get_process_statm_mb",
    "get_process_pss_mb",
    "read_kernel_cpu_percent",
    "read_kernel_meminfo",
    "read_kernel_uptime",
    "measure_latency_ns",
    "MockDBusPowerManagement"
]
