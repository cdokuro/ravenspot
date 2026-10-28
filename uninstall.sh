#!/usr/bin/env bash
# omarchy-menu KALDIRMA scripti — yapilan her degisikligi geri alir.
# Guvenli: sadece bu projenin olusturdugu dosyalari siler.
set -u

APP_DIR="$HOME/.local/share/omarchy-menu"
DESKTOP="$HOME/.local/share/applications/omarchy-menu.desktop"
WRAPPER="$HOME/.local/bin/omarchy-menu"

echo "== omarchy-menu kaldiriliyor =="

# 1) Calisan menuyu kapat
pkill -f "omarchy-menu/bridge.py" 2>/dev/null && echo "  - calisan menu kapatildi" || true

# 2) Masaustu kaydini kaldir (kisayol burada yasamisti)
if [ -f "$DESKTOP" ]; then
  rm -f "$DESKTOP"
  echo "  - masaustu kaydi silindi: $DESKTOP"
fi

# 3) Baslatici script
if [ -f "$WRAPPER" ]; then
  rm -f "$WRAPPER"
  echo "  - baslatici silindi: $WRAPPER"
fi

# 4) Uygulama dizini (menu.jsonc, Menu.qml, MenuModel.js, bridge.py, yedekler)
if [ -d "$APP_DIR" ]; then
  rm -rf "$APP_DIR"
  echo "  - uygulama dizini silindi: $APP_DIR"
fi

# 5) Masaustu veritabanini yenile
if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 >/dev/null 2>&1
  echo "  - masaustu veritabani yenilendi"
fi

echo
echo "Tamam. Omarchy Menu tamamen kaldirildi."
echo "Super+Space (Meta+Space) tekrar bossa dondu."
echo
echo "Not: kglobalshortcutsrc hic degistirilmemisti (X-KDE-Shortcuts mekanizmasi kullanildi),"
echo "bu yuzden ek bir geri alma gerekmiyor. Eski yedekler kaldirilirken silindi."
