import json
import unittest
import bridge


class TestKRunnerActions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PySide6.QtGui import QGuiApplication
        import sys
        if not QGuiApplication.instance():
            cls.app = QGuiApplication(sys.argv)
        cls.shell = bridge.Shell()

    def test_version_bump(self):
        self.assertEqual(self.shell.version, "0.2.18")
        self.assertEqual(bridge.__version__, "0.2.18")

    def test_power_profile_and_sleep_inhibit(self):
        self.assertTrue(hasattr(self.shell, "getPowerProfile"))
        self.assertTrue(hasattr(self.shell, "setPowerProfile"))
        self.assertTrue(hasattr(self.shell, "isSleepInhibited"))
        self.assertTrue(hasattr(self.shell, "setSleepInhibited"))
        prof = self.shell.getPowerProfile()
        self.assertIn(prof, ["power-saver", "balanced", "performance"])
        inh = self.shell.isSleepInhibited()
        self.assertIsInstance(inh, bool)

    def test_update_info(self):
        self.assertTrue(hasattr(self.shell, "getUpdateInfo"))
        self.assertTrue(hasattr(self.shell, "launchUpdate"))
        info = self.shell.getUpdateInfo()
        self.assertIsInstance(info, dict)
        self.assertIn("has_updates", info)
        self.assertIn("icon_path", info)
        self.assertIn("tooltip", info)

    def test_resolve_item_path(self):
        # Test downloads folder resolution
        p = self.shell.resolveItemPath("downloads", "files-downloads", "", "")
        self.assertTrue(len(p) > 0, "Downloads path should be resolved")
        self.assertTrue(self.shell.isDirectory(p), "Resolved downloads path should be a directory")

        # Test folder path getter
        dl = self.shell.getFolderPath("downloads")
        self.assertTrue(len(dl) > 0)

        # Test action with file path
        act_p = self.shell.resolveItemPath("", "", f"xdg-open {dl}", "")
        self.assertEqual(act_p, dl)

    def test_search_files(self):
        # Empty query returns empty array
        self.assertEqual(self.shell.searchFiles(""), "[]")
        # Search for bridge.py or similar known file
        res = json.loads(self.shell.searchFiles("bridge.py"))
        self.assertIsInstance(res, list)
        if res:
            first = res[0]
            self.assertIn("name", first)
            self.assertIn("path", first)
            self.assertIn("isDir", first)

    def test_kcm_indexing(self):
        # Search for shortcuts or kcm_keys in apps
        apps_json = self.shell.listApps()
        apps = json.loads(apps_json)
        kcm_items = [a for a in apps if "kcm" in a.get("id", "") or "kcm" in a.get("desktop", "")]
        self.assertTrue(len(kcm_items) > 0, "KDE KCM settings modules should be indexed")

    def test_list_processes_empty(self):
        res = self.shell.listProcesses("")
        self.assertEqual(res, "[]")

    def test_list_processes_matches(self):
        # Current python process or system process should match
        res_str = self.shell.listProcesses("python")
        procs = json.loads(res_str)
        self.assertIsInstance(procs, list)
        if procs:
            self.assertIn("pid", procs[0])
            self.assertIn("name", procs[0])

    def test_krunner_slots_exist(self):
        slots = [
            "powerOff", "rebootSystem", "lockScreen",
            "suspendSystem", "logoutUser", "restartPlasma",
            "listProcesses", "killProcess"
        ]
        for slot in slots:
            self.assertTrue(hasattr(self.shell, slot), f"Missing slot: {slot}")


    def test_open_system_monitor(self):
        # Verify method exists and runs without raising exceptions
        self.assertTrue(hasattr(self.shell, "openSystemMonitor"))
        try:
            # Test that it safely handles calls without errors
            # (Note: we don't assert processes, just verify no crash or unhandled exceptions)
            self.shell.openSystemMonitor("gpu")
            self.shell.openSystemMonitor("cpu")
            self.shell.openSystemMonitor("ram")
            self.shell.openSystemMonitor("disk")
        except Exception as e:
            self.fail(f"openSystemMonitor raised exception: {e}")

if __name__ == "__main__":
    unittest.main()
