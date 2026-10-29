import unittest
from unittest.mock import patch, MagicMock
import os
import sys

# Set testing flag
os.environ["RAVENSPOT_TESTING"] = "1"

from PySide6.QtGui import QGuiApplication
app = QGuiApplication.instance() or QGuiApplication(sys.argv)

from bridge import Shell


class TestWindowsActions(unittest.TestCase):
    def setUp(self):
        self.shell = Shell()

    @patch("bridge.IS_WINDOWS", True)
    @patch("subprocess.Popen")
    def test_open_path_directory(self, mock_popen):
        with patch("os.path.isdir", return_value=True):
            self.shell.openPath(r"C:\Users\User\Downloads")
            mock_popen.assert_called_once()
            args = mock_popen.call_args[0][0]
            self.assertEqual(args[0], "explorer.exe")
            self.assertIn("Downloads", args[1])

    @patch("bridge.IS_WINDOWS", True)
    @patch("bridge.os.startfile", create=True)
    def test_open_path_file(self, mock_startfile):
        with patch("os.path.isdir", return_value=False):
            self.shell.openPath(r"C:\Users\User\file.txt")
            mock_startfile.assert_called_once()

    @patch("bridge.IS_WINDOWS", True)
    @patch("subprocess.Popen")
    @patch("os.path.exists", return_value=True)
    @patch("os.path.isdir", return_value=False)
    def test_open_containing_folder_file(self, mock_isdir, mock_exists, mock_popen):
        self.shell.openContainingFolder(r"C:\Users\User\file.txt")
        mock_popen.assert_called_once()
        args = mock_popen.call_args[0][0]
        self.assertEqual(args[0], "explorer.exe")
        self.assertTrue(args[1].startswith("/select,"))

    @patch("bridge.IS_WINDOWS", True)
    @patch("subprocess.Popen")
    @patch("shutil.which")
    def test_open_in_terminal_wt(self, mock_which, mock_popen):
        mock_which.side_effect = lambda x: "/path/to/wt.exe" if "wt" in x else None
        with patch("os.path.isdir", return_value=True):
            self.shell.openInTerminal(r"C:\Users\User\Projects")
            mock_popen.assert_called_once()
            args = mock_popen.call_args[0][0]
            self.assertEqual(args[0], "wt.exe")
            self.assertEqual(args[1], "-d")

    @patch("bridge.IS_WINDOWS", True)
    @patch("subprocess.Popen")
    @patch("shutil.which")
    def test_open_in_terminal_powershell_fallback(self, mock_which, mock_popen):
        def fake_which(cmd):
            if "powershell" in cmd:
                return "powershell.exe"
            return None
        mock_which.side_effect = fake_which
        with patch("os.path.isdir", return_value=True):
            self.shell.openInTerminal(r"C:\Users\User\Projects")
            mock_popen.assert_called_once()
            args = mock_popen.call_args[0][0]
            self.assertEqual(args[0], "powershell.exe")


if __name__ == "__main__":
    unittest.main()
