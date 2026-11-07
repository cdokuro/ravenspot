#!/usr/bin/env python3
"""Stage the Windows distribution folder (used by .github/workflows/build-windows.yml).

The bundled candy-icons theme contains ~2700 symlinks. Windows checkouts turn
them into real symlinks or into small text files holding the target name, and
PowerShell's Copy-Item cannot copy either reliably. This copies every file with
its symlink resolved (a placeholder text file is replaced by its target).

usage: stage_windows.py <exe> <dist-dir>
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ["Menu.qml", "MenuModel.js", "CommandOutputPanel.qml", "OpenCloseAnimation.qml",
         "menu.jsonc", "themes.json", "README.md", "LICENSE", "VERSION"]
DIRS = ["sounds", "icons", "assets", "compat"]


def resolve(path, depth=0):
    """Real file behind a symlink or a git symlink placeholder, or None."""
    if depth > 8:
        return None
    if os.path.islink(path):
        target = os.path.join(os.path.dirname(path), os.readlink(path))
        return resolve(os.path.normpath(target), depth + 1)
    if not os.path.isfile(path):
        return None
    if os.path.getsize(path) < 256 and not path.endswith((".json", ".qml", ".js", ".theme")):
        with open(path, "rb") as f:
            data = f.read().strip()
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            return path
        if text and "\n" not in text and "<" not in text:
            candidate = os.path.normpath(os.path.join(os.path.dirname(path), text))
            if os.path.exists(candidate) and candidate != os.path.normpath(path):
                return resolve(candidate, depth + 1)
    return path


def main():
    exe, dist = sys.argv[1], sys.argv[2]
    os.makedirs(dist, exist_ok=True)
    shutil.copy2(exe, dist)
    for name in FILES:
        shutil.copy2(os.path.join(ROOT, name), dist)
    copied = skipped = 0
    for d in DIRS:
        for base, _dirs, files in os.walk(os.path.join(ROOT, d)):
            rel = os.path.relpath(base, ROOT)
            os.makedirs(os.path.join(dist, rel), exist_ok=True)
            for fn in files:
                src = resolve(os.path.join(base, fn))
                if src is None:
                    skipped += 1
                    continue
                shutil.copyfile(src, os.path.join(dist, rel, fn))
                copied += 1
    print(f"staged {copied} files ({skipped} dangling links skipped) into {dist}")


if __name__ == "__main__":
    main()
