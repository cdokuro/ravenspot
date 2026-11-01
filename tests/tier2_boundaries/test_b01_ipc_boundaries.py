import os
import socket
import tempfile
import time
import unittest
from tests.harness.daemon_client import RavenSpotIPCClient
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB01IPCBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 1 - IPC Socket & Daemon Toggle."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.sock_path = os.path.join(self.tmp_dir.name, "ipc_bound.sock")
        self.daemon = MockRavenSpotDaemon(self.sock_path)
        self.daemon.start()
        self.client = RavenSpotIPCClient(self.sock_path)
        time.sleep(0.05)

    def tearDown(self):
        self.daemon.stop()
        self.tmp_dir.cleanup()

    def test_01_ipc_empty_command(self):
        """Sending newline or whitespace only returns an error without crashing daemon."""
        success, resp, _ = self.client.send_command("\n")
        self.assertTrue(success)
        self.assertIn("error", resp.lower())

    def test_02_ipc_unknown_command(self):
        """Sending invalid/unknown command string returns error message cleanly."""
        success, resp, _ = self.client.send_command("unknown_operation_xyz_99\n")
        self.assertTrue(success)
        self.assertIn("unknown command", resp.lower())

    def test_03_ipc_oversized_payload(self):
        """Sending 64KB command string is handled within buffer limits without crash."""
        huge_payload = "A" * 65536 + "\n"
        success, resp, _ = self.client.send_command(huge_payload)
        self.assertTrue(success)

    def test_04_ipc_rapid_burst_connections(self):
        """Rapidly opening and closing 50 connections does not exhaust socket queue."""
        for _ in range(50):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(self.sock_path)
            s.sendall(b"status\n")
            s.recv(512)
            s.close()
        # Verify daemon is still operational
        success, resp, _ = self.client.toggle()
        self.assertTrue(success)

    def test_05_ipc_partial_write_disconnect(self):
        """Abrupt disconnect mid-command does not crash or leave zombie state."""
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.sock_path)
        s.sendall(b"tog")  # Partial write
        s.close()  # Abrupt close
        time.sleep(0.02)

        # Subsequent valid command must succeed
        success, resp, _ = self.client.show()
        self.assertTrue(success)
        self.assertEqual(resp, "ok")

if __name__ == "__main__":
    unittest.main()
