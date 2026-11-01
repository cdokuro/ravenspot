import json
import os
import socket
import time
from typing import Tuple, Optional, Dict, Any

class RavenSpotIPCClient:
    """
    Opaque-box IPC client for RavenSpot launcher daemon.
    Communicates over Unix domain socket ($XDG_RUNTIME_DIR/ravenspot.sock or /tmp/ravenspot.sock).
    """

    def __init__(self, socket_path: Optional[str] = None, timeout: float = 1.0):
        if socket_path:
            self.socket_path = socket_path
        else:
            xdg_runtime = os.environ.get("XDG_RUNTIME_DIR")
            if xdg_runtime and os.path.exists(xdg_runtime):
                self.socket_path = os.path.join(xdg_runtime, "ravenspot.sock")
            else:
                self.socket_path = "/tmp/ravenspot.sock"
        self.timeout = timeout

    def is_socket_present(self) -> bool:
        """Check whether socket file exists and is a socket."""
        if not os.path.exists(self.socket_path):
            return False
        import stat
        try:
            mode = os.stat(self.socket_path).st_mode
            return stat.S_ISSOCK(mode)
        except OSError:
            return False

    def send_command(self, cmd: str) -> Tuple[bool, str, float]:
        """
        Send a newline-terminated command and return (success, response_string, latency_ms).
        """
        start = time.perf_counter_ns()
        if not cmd.endswith("\n"):
            cmd += "\n"

        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        try:
            s.connect(self.socket_path)
            s.sendall(cmd.encode("utf-8"))

            chunks = []
            while True:
                try:
                    data = s.recv(4096)
                    if not data:
                        break
                    chunks.append(data)
                    if b"\n" in data:
                        break
                except socket.timeout:
                    break
            end = time.perf_counter_ns()
            latency_ms = (end - start) / 1_000_000.0
            response = b"".join(chunks).decode("utf-8", errors="replace").strip()
            return True, response, latency_ms
        except Exception as e:
            end = time.perf_counter_ns()
            latency_ms = (end - start) / 1_000_000.0
            return False, str(e), latency_ms
        finally:
            try:
                s.close()
            except Exception:
                pass

    def send_raw(self, raw_bytes: bytes) -> Tuple[bool, bytes, float]:
        """Send raw bytes over socket for boundary/fuzzing tests."""
        start = time.perf_counter_ns()
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        try:
            s.connect(self.socket_path)
            s.sendall(raw_bytes)
            chunks = []
            while True:
                try:
                    data = s.recv(4096)
                    if not data:
                        break
                    chunks.append(data)
                    if len(data) < 4096:
                        break
                except socket.timeout:
                    break
            end = time.perf_counter_ns()
            latency_ms = (end - start) / 1_000_000.0
            return True, b"".join(chunks), latency_ms
        except Exception as e:
            end = time.perf_counter_ns()
            latency_ms = (end - start) / 1_000_000.0
            return False, str(e).encode(), latency_ms
        finally:
            try:
                s.close()
            except Exception:
                pass

    def toggle(self) -> Tuple[bool, str, float]:
        return self.send_command("toggle")

    def show(self) -> Tuple[bool, str, float]:
        return self.send_command("show")

    def hide(self) -> Tuple[bool, str, float]:
        return self.send_command("hide")

    def quit(self) -> Tuple[bool, str, float]:
        return self.send_command("quit")

    def status(self) -> Tuple[bool, Dict[str, Any], float]:
        success, resp, latency = self.send_command("status")
        if not success:
            return False, {}, latency
        try:
            parsed = json.loads(resp)
            return True, parsed, latency
        except json.JSONDecodeError:
            return False, {"raw": resp}, latency
