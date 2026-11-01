import os
import tempfile
import time
import unittest
from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestF01IPCDaemon(unittest.TestCase):
    """Feature 1: IPC Socket & Daemon Toggle (R1, R2, AC)."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.sock_path = os.path.join(self.tmp_dir.name, "ravenspot_test.sock")
        self.daemon = MockRavenSpotDaemon(self.sock_path)
        self.daemon.start()
        self.client = RavenSpotIPCClient(self.sock_path)
        time.sleep(0.05)

    def tearDown(self):
        self.daemon.stop()
        self.tmp_dir.cleanup()

    def test_01_socket_file_creation(self):
        """Verifies socket exists at expected path with valid socket mode."""
        self.assertTrue(self.client.is_socket_present(), f"Socket file not found at {self.sock_path}")

    def test_02_ipc_toggle_command(self):
        """Sends 'toggle', verifies 'ok' response and visibility state flips."""
        self.assertFalse(self.daemon.visible)
        success, resp, lat = self.client.toggle()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertTrue(self.daemon.visible)

        # Toggle back
        success, resp, lat = self.client.toggle()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertFalse(self.daemon.visible)

    def test_03_ipc_show_command(self):
        """Sends 'show', verifies 'ok' response and visibility becomes true."""
        self.daemon.visible = False
        success, resp, _ = self.client.show()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertTrue(self.daemon.visible)

    def test_04_ipc_hide_command(self):
        """Sends 'hide', verifies 'ok' response and visibility becomes false."""
        self.daemon.visible = True
        success, resp, _ = self.client.hide()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertFalse(self.daemon.visible)

    def test_05_ipc_status_command(self):
        """Sends 'status', verifies JSON payload containing visible, uptime, and theme."""
        success, status_dict, _ = self.client.status()
        self.assertTrue(success)
        self.assertIn("visible", status_dict)
        self.assertIn("uptime", status_dict)
        self.assertIn("theme", status_dict)
        self.assertIsInstance(status_dict["visible"], bool)
        self.assertIsInstance(status_dict["uptime"], int)
        self.assertIsInstance(status_dict["theme"], str)

if __name__ == "__main__":
    unittest.main()
