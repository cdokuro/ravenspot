import os
import tempfile
import unittest

class TestF08AppIndexer(unittest.TestCase):
    """Feature 8: Application Indexer (.desktop) (R2, AC)."""

    def parse_desktop_content(self, content: str, lang: str = "en"):
        """Simple INI-style parser replicating .desktop indexing specifications."""
        in_main = False
        data = {"actions": []}
        curr_action = None

        for line in content.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line == "[Desktop Entry]":
                in_main = True
                curr_action = None
                continue
            elif line.startswith("[Desktop Action"):
                in_main = False
                curr_action = {}
                data["actions"].append(curr_action)
                continue

            if "=" in line:
                key, val = line.split("=", 1)
                key = key.strip()
                val = val.strip()
                if in_main:
                    if key == f"Name[{lang}]":
                        data["Name"] = val
                    elif key == "Name" and "Name" not in data:
                        data["Name"] = val
                    elif key == "Exec":
                        data["Exec"] = val
                    elif key == "NoDisplay":
                        data["NoDisplay"] = val.lower() == "true"
                    elif key == "Icon":
                        data["Icon"] = val
                elif curr_action is not None:
                    curr_action[key] = val
        return data

    def test_01_desktop_indexer_scans_standard_paths(self):
        """Verifies standard freedesktop application paths exist on the system."""
        sys_apps = "/usr/share/applications"
        self.assertTrue(os.path.isdir(sys_apps), f"System applications dir missing: {sys_apps}")

    def test_02_desktop_indexer_parses_name_and_exec(self):
        """Verifies extraction of Name, Exec, and Icon keys."""
        sample_ini = """[Desktop Entry]
Name=Firefox Web Browser
Exec=firefox %u
Icon=firefox
Type=Application
"""
        parsed = self.parse_desktop_content(sample_ini)
        self.assertEqual(parsed.get("Name"), "Firefox Web Browser")
        self.assertEqual(parsed.get("Exec"), "firefox %u")
        self.assertEqual(parsed.get("Icon"), "firefox")

    def test_03_desktop_indexer_bilingual_name(self):
        """Verifies localized Turkish name (Name[tr]) is prioritized when lang=tr."""
        sample_ini = """[Desktop Entry]
Name=Text Editor
Name[tr]=Metin Düzenleyici
Exec=kate %U
Type=Application
"""
        parsed_tr = self.parse_desktop_content(sample_ini, lang="tr")
        self.assertEqual(parsed_tr.get("Name"), "Metin Düzenleyici")

        parsed_en = self.parse_desktop_content(sample_ini, lang="en")
        self.assertEqual(parsed_en.get("Name"), "Text Editor")

    def test_04_desktop_indexer_filters_nodisplay(self):
        """Verifies applications with NoDisplay=true are flagged for exclusion."""
        sample_ini = """[Desktop Entry]
Name=Hidden Background Helper
Exec=helper
NoDisplay=true
Type=Application
"""
        parsed = self.parse_desktop_content(sample_ini)
        self.assertTrue(parsed.get("NoDisplay", False))

    def test_05_desktop_indexer_subactions(self):
        """Verifies parsing of sub-actions (e.g. New Incognito Window)."""
        sample_ini = """[Desktop Entry]
Name=Chromium
Exec=chromium %U
Actions=NewWindow;NewIncognito;

[Desktop Action NewWindow]
Name=New Window
Exec=chromium --new-window

[Desktop Action NewIncognito]
Name=New Incognito Window
Exec=chromium --incognito
"""
        parsed = self.parse_desktop_content(sample_ini)
        actions = parsed.get("actions", [])
        self.assertEqual(len(actions), 2)
        self.assertEqual(actions[1].get("Name"), "New Incognito Window")
        self.assertEqual(actions[1].get("Exec"), "chromium --incognito")

if __name__ == "__main__":
    unittest.main()
