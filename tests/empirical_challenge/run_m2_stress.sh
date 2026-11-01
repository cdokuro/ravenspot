#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${REPO_ROOT}"

echo "======================================================================"
echo "Compiling and Executing Milestone 2 Adversarial Empirical Stress Suite"
echo "======================================================================"

g++ -std=c++20 -O2 -fPIC -mno-direct-extern-access \
  -I. -Isrc -I/usr/include/LayerShellQt -I/usr/include/KF6/KWindowSystem \
  -I/usr/include/qt6 -I/usr/include/qt6/QtCore -I/usr/include/qt6/QtGui \
  -I/usr/include/qt6/QtQuick -I/usr/include/qt6/QtMultimedia -I/usr/include/qt6/QtQml \
  -I/usr/include/qt6/QtNetwork -I/usr/include/qt6/QtDBus \
  tests/empirical_challenge/test_stress_m2_empirical.cpp \
  LayerShellWindow.o moc_LayerShellWindow.o ThemeManager.o IconProviders.o \
  ShellBridge.o AudioEngine.o SoundConfig.o moc_ThemeManager.o moc_ShellBridge.o \
  moc_AudioEngine.o moc_SoundConfig.o \
  -lLayerShellQtInterface -lKF6WindowSystem -lQt6Gui -lQt6Core \
  -lQt6Quick -lQt6Multimedia -lQt6Qml -lQt6Network -lQt6DBus \
  -o tests/empirical_challenge/test_stress_m2_empirical

./tests/empirical_challenge/test_stress_m2_empirical
