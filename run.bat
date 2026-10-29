@echo off
setlocal
echo RavenSpot baslatiliyor...
if exist dist\RavenSpot.exe (
    start "" dist\RavenSpot.exe
) else (
    python bridge.py
)
