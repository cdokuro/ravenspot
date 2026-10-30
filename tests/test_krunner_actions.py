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
        self.assertEqual(self.shell.version, "0.2.2")
        self.assertEqual(bridge.__version__, "0.2.2")

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


if __name__ == "__main__":
    unittest.main()
