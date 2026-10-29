@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo        RavenSpot - Windows 11 Build Script
echo ===================================================

python --version >nul 2>&1
if errorlevel 1 (
    echo [HATA] Python bulunamadi! Lutfen Python 3.10+ yukleyin ve PATH'e ekleyin.
    pause
    exit /b 1
)

echo.
echo [1/3] Bagimliliklar kontrol ediliyor ve yukleniyor...
pip install --upgrade pip
pip install -r requirements.txt pyinstaller

echo.
echo [2/3] PyInstaller ile RavenSpot.exe derleniyor...
pyinstaller --noconsole --onefile --name "RavenSpot" ^
    --add-data "Menu.qml;." ^
    --add-data "MenuModel.js;." ^
    --add-data "menu.jsonc;." ^
    --add-data "themes.json;." ^
    --add-data "sounds;sounds" ^
    --add-data "icons;icons" ^
    --clean ^
    bridge.py

if errorlevel 1 (
    echo.
    echo [HATA] Derleme basarisiz oldu!
    pause
    exit /b 1
)

echo.
echo [3/3] Derleme basariyla tamamlandi!
echo ===================================================
echo Exe konumu: dist\RavenSpot.exe
echo RavenSpot'u baslatmak icin dist\RavenSpot.exe calistirabilirsiniz.
echo Kisayol: Alt+Space veya Win+Space
echo ===================================================
pause
