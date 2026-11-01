# ==============================================================================
# RavenSpot - Native C++20 Desktop Launcher & Resident Daemon
# Build Configuration (QMake6 / Qt 6.12.0)
# ==============================================================================

QT += core gui quick qml dbus multimedia network
CONFIG += c++20 release link_pkgconfig

# FreeDesktop & KDE Frameworks integration
PKGCONFIG += KF6WindowSystem

# Wayland Layer-Shell Qt interface
INCLUDEPATH += /usr/include/LayerShellQt
LIBS += -lLayerShellQtInterface

# Include project source roots
INCLUDEPATH += src

# Target executable
TARGET = ravenspot
TEMPLATE = app

# Compiler Optimization & Strict Warnings
# Note: -mno-direct-extern-access and -fPIC are required for GCC on Arch/CachyOS
QMAKE_CXXFLAGS += -std=c++20 -O2 -pipe -Wall -Wextra -Wpedantic -mno-direct-extern-access -fPIC
QMAKE_LFLAGS += -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now

# Milestone 1 & 3 Source Files
HEADERS += \
    src/core/DaemonServer.hpp \
    src/core/LayerShellWindow.hpp \
    src/core/SingleInstance.hpp \
    src/ui/ShellBridge.hpp \
    src/ui/ThemeManager.hpp \
    src/ui/IconProviders.hpp \
    src/audio/AudioEngine.hpp \
    src/audio/SoundConfig.hpp \
    src/search/SearchResultItem.hpp \
    src/search/AppIndexer.hpp \
    src/search/KRunnerActions.hpp \
    src/search/CalcConverter.hpp

SOURCES += \
    src/main.cpp \
    src/core/DaemonServer.cpp \
    src/core/LayerShellWindow.cpp \
    src/core/SingleInstance.cpp \
    src/ui/ShellBridge.cpp \
    src/ui/ThemeManager.cpp \
    src/ui/IconProviders.cpp \
    src/audio/AudioEngine.cpp \
    src/audio/SoundConfig.cpp \
    src/search/AppIndexer.cpp \
    src/search/KRunnerActions.cpp \
    src/search/CalcConverter.cpp
