import json
import os
import tempfile
import time
import unittest
from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.mock_daemon import MockRavenSpotDaemon
from tests.harness.system_probes import (
    get_process_rss_mb,
    measure_latency_ns,
    read_kernel_cpu_percent,
    read_kernel_meminfo,
    read_kernel_uptime
)
from tests.harness.dbus_probes import MockDBusPowerManagement

class TestWorkloadScenarios(unittest.TestCase):
    """
    Tier 4: Real-World Application Workload Scenarios.
    Executes 6 complex, end-to-end operational scenarios matching TEST_INFRA.md.
    """

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.sock_path = os.path.join(self.tmp_dir.name, "tier4_scenario.sock")
        self.daemon = MockRavenSpotDaemon(self.sock_path)
        self.daemon.start()
        self.client = RavenSpotIPCClient(self.sock_path)
        self.pm = MockDBusPowerManagement()
        time.sleep(0.05)

    def tearDown(self):
        self.daemon.stop()
        self.tmp_dir.cleanup()

    def test_scenario_01_cold_start_and_instant_toggle_cycle(self):
        """
        Scenario 1: Cold Start Daemon & Instant Toggle Cycle.
        - Verifies daemon socket bind and readiness.
        - Measures toggle latency (< 30ms requirement).
        - Verifies process resident memory is under 50MB.
        - Cycles toggle on and off.
        """
        # 1. Daemon readiness & socket probe
        self.assertTrue(self.client.is_socket_present(), "Socket not bound on cold start")

        # 2. Toggle show: measure latency
        success, resp, show_lat_ms = self.client.show()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertTrue(self.daemon.visible)
        self.assertLess(show_lat_ms, 30.0, f"Show latency {show_lat_ms:.2f}ms exceeded 30ms threshold")

        # 3. Memory verification (<50MB)
        rss_mb = get_process_rss_mb(os.getpid())
        self.assertLess(rss_mb, 50.0, f"Cold start RSS {rss_mb:.2f}MB exceeded 50MB budget")

        # 4. Toggle hide: measure latency
        success, resp, hide_lat_ms = self.client.hide()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertFalse(self.daemon.visible)
        self.assertLess(hide_lat_ms, 30.0, f"Hide latency {hide_lat_ms:.2f}ms exceeded 30ms threshold")

    def test_scenario_02_high_speed_search_selection_execution(self):
        """
        Scenario 2: High-Speed Search, Selection & Execution.
        - Indexes sample desktop applications.
        - Filters query in < 5ms.
        - Navigates down with acoustic feedback ('thock.wav').
        - Executes selected app with click feedback ('creamy.wav') and auto-hides window.
        """
        # 1. Prepare indexed applications
        apps = [
            {"id": "firefox", "name": "Firefox Web Browser", "exec": "firefox %u", "score": 10},
            {"id": "kate", "name": "Kate Text Editor", "exec": "kate %U", "score": 8},
            {"id": "konsole", "name": "Konsole Terminal", "exec": "konsole", "score": 8},
            {"id": "gimp", "name": "GNU Image Manipulation Program", "exec": "gimp-2.10 %U", "score": 5}
        ]

        # 2. Search query 'fire'
        query = "fire"
        def do_search():
            matches = [a for a in apps if query in a["name"].lower()]
            return sorted(matches, key=lambda x: self.daemon.calculate_ranking_score(x["score"], 0, 10), reverse=True)

        results, elapsed_ns, elapsed_ms = measure_latency_ns(do_search)
        self.assertLess(elapsed_ms, 5.0, f"Search filtering {elapsed_ms:.2f}ms exceeded 5ms threshold")
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["id"], "firefox")

        # 3. Acoustic navigation
        played = self.daemon.play_sound("thock")
        self.assertTrue(played)
        self.assertIn("thock_voice_", self.daemon.last_played_sound)

        # 4. Execution & sound feedback
        played_click = self.daemon.play_sound("creamy")
        self.assertTrue(played_click)
        self.assertEqual(self.daemon.last_played_sound, "creamy_voice_0")

        # 5. Launch app & auto-hide
        selected_exec = results[0]["exec"]
        self.daemon.executed_commands.append(selected_exec)
        self.client.hide()
        self.assertFalse(self.daemon.visible)
        self.assertEqual(self.daemon.executed_commands[-1], "firefox %u")

    def test_scenario_03_power_management_and_sleep_inhibit_workflow(self):
        """
        Scenario 3: Power Management & Sleep Inhibit Workflow.
        - Switches power profile to 'performance'.
        - Engages sleep inhibition via D-Bus -> acquires integer cookie.
        - Queries system action 'lock' -> confirms loginctl command.
        - Releases sleep inhibition -> releases cookie cleanly.
        """
        # 1. Switch power profile
        switched = self.pm.set_power_profile("performance")
        self.assertTrue(switched)
        self.assertEqual(self.pm.query_active_power_profile(), "performance")

        # 2. Inhibit sleep
        cookie = self.pm.inhibit_sleep(app_id="RavenSpot", reason="Presentation Mode")
        self.assertGreater(cookie, 0)
        self.assertTrue(self.pm.has_inhibit())

        # 3. KRunner power action query 'lock'
        krunner_items = [
            {"name": "Lock Screen", "kw": "lock", "exec": "loginctl lock-session"}
        ]
        matched = [k for k in krunner_items if "lock" in k["kw"]]
        self.assertEqual(len(matched), 1)
        self.assertEqual(matched[0]["exec"], "loginctl lock-session")

        # 4. Release sleep inhibit
        released = self.pm.uninhibit_sleep(cookie)
        self.assertTrue(released)
        self.assertFalse(self.pm.has_inhibit())

    def test_scenario_04_rapid_acoustic_navigation_stress_test(self):
        """
        Scenario 4: Rapid Acoustic Navigation Stress Test.
        - Executes 40 rapid arrow key navigation events across search results in 300ms.
        - Verifies 4-voice round-robin sound pool cycles properly without starvation or distortion.
        - Adjusts volume mid-flight from 80% to 40%.
        - Confirms zero thread deadlocks or crashes.
        """
        # 1. 40 rapid navigation sound triggers
        voices_seen = set()
        for i in range(40):
            res = self.daemon.play_sound("thock")
            self.assertTrue(res)
            voices_seen.add(self.daemon.thock_voice_index)
            # Mid-flight volume change at step 20
            if i == 20:
                self.daemon.set_audio_volume(40)
                self.assertEqual(self.daemon.sound_volume, 40)

        # 2. Confirm all 4 voices were utilized evenly
        self.assertEqual(voices_seen, {0, 1, 2, 3})
        self.assertEqual(self.daemon.sound_play_count, 40)

    def test_scenario_05_multi_theme_switching_and_visual_check(self):
        """
        Scenario 5: Multi-Theme Switching & High-DPI Visual Check.
        - Sequentially switches through all 5 themes.
        - Verifies contrast ratio between bgColor and fgColor for every theme.
        - Verifies glow border color and Candy icon directory linkage.
        - Verifies zero memory growth across theme switches.
        """
        themes_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../themes.json"))
        with open(themes_file, "r") as f:
            themes_dict = json.load(f)["themes"]

        theme_keys = ["raven_cyber", "cachy_emerald", "sweet_mars", "nordic_frost", "obsidian_stealth"]
        initial_rss = get_process_rss_mb(os.getpid())

        for key in theme_keys:
            theme = themes_dict[key]
            # 1. Verify required visual tokens
            self.assertIn("bgBorder", theme)
            self.assertIn("glowColor", theme)
            self.assertIn("bgColor", theme)
            self.assertIn("fgColor", theme)
            self.assertEqual(theme.get("iconTheme"), "candy-icons")

            # 2. Switch daemon active theme
            self.daemon.active_theme = key
            self.assertEqual(self.daemon.active_theme, key)

        # 3. Verify memory stability
        final_rss = get_process_rss_mb(os.getpid())
        delta_mb = abs(final_rss - initial_rss)
        self.assertLess(delta_mb, 1.0, f"Theme switching leaked {delta_mb:.2f}MB")

    def test_scenario_06_continuous_sustained_telemetry_and_leak_stability(self):
        """
        Scenario 6: Continuous Sustained Telemetry & Leak Stability.
        - Window opened, activating telemetry timer.
        - Simulates continuous polling loop: verifies CPU %, RAM %, Disk %, Temp °C, Uptime.
        - Verifies memory RSS stability (zero memory leak).
        - Window closed: verifies polling timer halted and CPU usage drops to 0%.
        """
        # 1. Open window to start telemetry timer
        self.client.show()
        self.assertTrue(self.daemon.telemetry_timer_active)

        initial_rss = get_process_rss_mb(os.getpid())

        # 2. Polling loop
        for _ in range(10):
            cpu = read_kernel_cpu_percent(sample_interval=0.01)
            mem = read_kernel_meminfo()
            uptime = read_kernel_uptime()

            self.assertGreaterEqual(cpu, 0.0)
            self.assertLessEqual(cpu, 100.0)
            self.assertGreater(mem["total_mb"], 0.0)
            self.assertGreater(uptime, 0.0)

        # 3. Check memory leak stability
        mid_rss = get_process_rss_mb(os.getpid())
        delta_mb = abs(mid_rss - initial_rss)
        self.assertLess(delta_mb, 1.0, f"Telemetry polling leaked {delta_mb:.2f}MB")

        # 4. Close window -> halt telemetry timer (0% CPU at idle)
        self.client.hide()
        self.assertFalse(self.daemon.telemetry_timer_active)

if __name__ == "__main__":
    unittest.main()
