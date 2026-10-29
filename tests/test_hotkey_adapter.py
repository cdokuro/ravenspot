import unittest
from unittest.mock import patch, MagicMock
import os
import sys

os.environ["RAVENSPOT_TESTING"] = "1"

from PySide6.QtGui import QGuiApplication
app = QGuiApplication.instance() or QGuiApplication(sys.argv)

from bridge import Win32HotkeyThread


class TestHotkeyAdapter(unittest.TestCase):
    def test_hotkey_thread_instantiation(self):
        thread = Win32HotkeyThread()
        self.assertFalse(thread.isRunning())
        thread.stop()

    @patch("bridge.IS_WINDOWS", True)
    def test_hotkey_thread_win32_mocked_loop(self):
        mock_ctypes = MagicMock()
        mock_user32 = MagicMock()
        mock_kernel32 = MagicMock()
        mock_kernel32.GetCurrentThreadId.return_value = 12345
        mock_ctypes.windll.user32 = mock_user32
        mock_ctypes.windll.kernel32 = mock_kernel32
        
        fake_msg = MagicMock()
        fake_msg.message = 0x0312  # WM_HOTKEY
        mock_ctypes.wintypes.MSG.return_value = fake_msg

        call_count = [0]
        def fake_get_msg(*args):
            call_count[0] += 1
            return 1 if call_count[0] == 1 else 0
        mock_user32.GetMessageW.side_effect = fake_get_msg

        thread = Win32HotkeyThread()
        signals_received = []
        thread.hotkeyTriggered.connect(lambda: signals_received.append(True))

        with patch.dict("sys.modules", {"ctypes": mock_ctypes, "ctypes.wintypes": mock_ctypes.wintypes}):
            thread.run()

        self.assertEqual(len(signals_received), 1)
        self.assertTrue(mock_user32.RegisterHotKey.called)
        self.assertTrue(mock_user32.UnregisterHotKey.called)


if __name__ == "__main__":
    unittest.main()
