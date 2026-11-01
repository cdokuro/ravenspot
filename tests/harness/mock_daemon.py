import json
import math
import os
import re
import socket
import threading
import time
from typing import Optional, Dict, Any, List

class MockRavenSpotDaemon:
    """
    High-fidelity reference daemon and test oracle simulating the RavenSpot
    daemon's socket IPC protocol, search ranking, audio engine, telemetry lifecycle,
    and power management contracts.
    """

    def __init__(self, socket_path: str):
        self.socket_path = socket_path
        self.running = False
        self.server_socket: Optional[socket.socket] = None
        self.thread: Optional[threading.Thread] = None

        # State matching PROJECT.md interface contracts
        self.visible = False
        self.start_time = time.time()
        self.active_theme = "raven_cyber"
        self.sound_volume = 80
        self.sound_muted = False
        self.power_profile = "balanced"
        self.sleep_inhibited = False
        self.inhibit_cookie = 1001
        self.telemetry_timer_active = False
        self.thock_voice_index = 0
        self.sound_play_count = 0
        self.last_played_sound = None
        self.active_selection_index = 0
        self.clipboard_content = ""
        self.executed_commands: List[str] = []

    def start(self):
        """Start listening on Unix domain socket."""
        if os.path.exists(self.socket_path):
            try:
                os.unlink(self.socket_path)
            except OSError:
                pass

        self.server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_socket.bind(self.socket_path)
        self.server_socket.listen(10)
        self.running = True
        self.thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop listening and clean up socket."""
        self.running = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception:
                pass
        if os.path.exists(self.socket_path):
            try:
                os.unlink(self.socket_path)
            except OSError:
                pass
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=1.0)

    def _accept_loop(self):
        while self.running:
            try:
                self.server_socket.settimeout(0.2)
                client_sock, _ = self.server_socket.accept()
                client_handler = threading.Thread(target=self._handle_client, args=(client_sock,), daemon=True)
                client_handler.start()
            except socket.timeout:
                continue
            except Exception:
                break

    def _handle_client(self, client_sock: socket.socket):
        try:
            client_sock.settimeout(1.0)
            data = client_sock.recv(65536)
            if not data:
                return

            text = data.decode("utf-8", errors="replace").strip()
            response = self.process_command(text)
            if response is not None:
                if not response.endswith("\n"):
                    response += "\n"
                client_sock.sendall(response.encode("utf-8"))
        except Exception:
            pass
        finally:
            try:
                client_sock.close()
            except Exception:
                pass

    def process_command(self, cmd: str) -> str:
        """Process incoming IPC command string."""
        cmd_clean = cmd.strip()
        if not cmd_clean:
            return "error: empty command"

        if cmd_clean == "toggle":
            self.visible = not self.visible
            self.telemetry_timer_active = self.visible
            return "ok"
        elif cmd_clean == "show":
            self.visible = True
            self.telemetry_timer_active = True
            return "ok"
        elif cmd_clean == "hide":
            self.visible = False
            self.telemetry_timer_active = False
            return "ok"
        elif cmd_clean == "quit":
            self.visible = False
            self.telemetry_timer_active = False
            # Trigger clean shutdown in background
            threading.Thread(target=self.stop, daemon=True).start()
            return "ok"
        elif cmd_clean == "status":
            uptime = int(time.time() - self.start_time)
            status_data = {
                "visible": self.visible,
                "uptime": uptime,
                "theme": self.active_theme,
                "power_profile": self.power_profile,
                "sleep_inhibited": self.sleep_inhibited,
                "sound_volume": self.sound_volume,
                "sound_muted": self.sound_muted,
                "telemetry_active": self.telemetry_timer_active
            }
            return json.dumps(status_data)
        else:
            return f"error: unknown command '{cmd_clean}'"

    # ── Subsystem Simulation Methods ──────────────────────────────────────────

    def play_sound(self, sound_type: str) -> bool:
        """Simulate polyphonic round-robin sound player."""
        if self.sound_muted or self.sound_volume <= 0:
            return False

        if sound_type == "thock":
            # 4 voices round-robin
            self.thock_voice_index = (self.thock_voice_index + 1) % 4
            self.last_played_sound = f"thock_voice_{self.thock_voice_index}"
        elif sound_type == "creamy":
            self.last_played_sound = "creamy_voice_0"
        else:
            return False

        self.sound_play_count += 1
        return True

    def set_audio_volume(self, volume: int):
        self.sound_volume = max(0, min(100, int(volume)))

    def set_audio_muted(self, muted: bool):
        self.sound_muted = bool(muted)

    def set_power_profile(self, profile: str) -> bool:
        valid_profiles = ["power-saver", "balanced", "performance"]
        if profile in valid_profiles:
            self.power_profile = profile
            return True
        return False

    def set_sleep_inhibited(self, inhibit: bool) -> int:
        self.sleep_inhibited = bool(inhibit)
        if self.sleep_inhibited:
            self.inhibit_cookie += 1
            return self.inhibit_cookie
        return 0

    def calculate_ranking_score(self, score: int, depth: int, order: int) -> int:
        """Ranking score formula from PROJECT.md: score * 1000 + depth * 25 + order"""
        return score * 1000 + depth * 25 + order

    def evaluate_math_expression(self, expr: str) -> Optional[str]:
        """In-line math evaluator with percent support."""
        expr_clean = expr.strip()
        # Percent support: '20% of 150' -> (20 / 100) * 150
        percent_match = re.match(r"^(\d+(?:\.\d+)?)\s*%\s+of\s+(\d+(?:\.\d+)?)$", expr_clean, re.IGNORECASE)
        if percent_match:
            pct = float(percent_match.group(1))
            val = float(percent_match.group(2))
            res = (pct / 100.0) * val
            return f"{res:g}"

        # Standard arithmetic: only allow digits, operators, parens, dots, spaces
        if re.match(r"^[\d\+\-\*\/\(\)\.\s]+$", expr_clean):
            try:
                # Disallow double asterisks (power) for safety
                if "**" in expr_clean or "//" in expr_clean:
                    return None
                val = eval(expr_clean, {"__builtins__": {}}, {})
                if math.isnan(val) or math.isinf(val):
                    return "undefined"
                return f"{val:g}"
            except ZeroDivisionError:
                return "undefined"
            except Exception:
                return None
        return None

    def evaluate_unit_conversion(self, query: str) -> Optional[str]:
        """Unit conversion for length, data, temperature, mass."""
        query_clean = query.strip().lower()
        match = re.match(r"^(\d+(?:\.\d+)?)\s*([a-z]+)\s+to\s+([a-z]+)$", query_clean)
        if not match:
            return None

        val = float(match.group(1))
        src_unit = match.group(2)
        dst_unit = match.group(3)

        # Length: km <-> miles
        if src_unit in ("km", "kilometer", "kilometers") and dst_unit in ("mi", "mile", "miles"):
            return f"{val * 0.621371:.2f} miles"
        if src_unit in ("mi", "mile", "miles") and dst_unit in ("km", "kilometer", "kilometers"):
            return f"{val / 0.621371:.2f} km"

        # Data: mb <-> gb
        if src_unit in ("mb", "megabytes") and dst_unit in ("gb", "gigabytes"):
            return f"{val / 1024.0:g} GB"
        if src_unit in ("gb", "gigabytes") and dst_unit in ("mb", "megabytes"):
            return f"{val * 1024.0:g} MB"

        # Temp: c <-> f
        if src_unit in ("c", "celsius") and dst_unit in ("f", "fahrenheit"):
            return f"{(val * 9.0/5.0) + 32.0:g} °F"
        if src_unit in ("f", "fahrenheit") and dst_unit in ("c", "celsius"):
            return f"{(val - 32.0) * 5.0/9.0:g} °C"

        # Mass: kg <-> lbs
        if src_unit in ("kg", "kilograms") and dst_unit in ("lb", "lbs", "pounds"):
            return f"{val * 2.20462:.2f} lbs"
        if src_unit in ("lb", "lbs", "pounds") and dst_unit in ("kg", "kilograms"):
            return f"{val / 2.20462:.2f} kg"

        return None
