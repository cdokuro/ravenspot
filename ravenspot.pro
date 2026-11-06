# ==============================================================================
# RavenSpot - Native C++20 Desktop Launcher & Resident Daemon
# Build Configuration (QMake6 / Qt 6.12.0)
# ==============================================================================

QT += core gui quick qml dbus multimedia network
CONFIG += c++20 release link_pkgconfig

# FreeDesktop & KDE Frameworks integration
PKGCONFIG += KF6WindowSystem wayland-client

# Wayland protocol bindings (vendored XML -> wayland-scanner at build time)
WAYLAND_PROTOCOLS = protocols/relative-pointer-unstable-v1.xml

wayland_header.input = WAYLAND_PROTOCOLS
wayland_header.output = ${QMAKE_FILE_BASE}-client-protocol.h
wayland_header.commands = wayland-scanner client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_header.variable_out = HEADERS
wayland_header.CONFIG += no_link target_predeps

wayland_code.input = WAYLAND_PROTOCOLS
wayland_code.output = ${QMAKE_FILE_BASE}-protocol.c
wayland_code.commands = wayland-scanner private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_code.variable_out = SOURCES
wayland_code.depends = ${QMAKE_FILE_BASE}-client-protocol.h

QMAKE_EXTRA_COMPILERS += wayland_header wayland_code
INCLUDEPATH += $$OUT_PWD

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
    src/core/RelativePointer.hpp \
    src/ui/ShellBridge.hpp \
    src/ui/ThemeManager.hpp \
    src/ui/IconProviders.hpp \
    src/audio/AudioEngine.hpp \
    src/audio/SoundConfig.hpp \
    src/search/SearchResultItem.hpp \
    src/search/AppIndexer.hpp \
    src/search/KRunnerActions.hpp \
    src/search/CalcConverter.hpp \
    src/run/CommandAnalyzer.hpp \
    src/run/CommandRunner.hpp \
    src/run/CommandHistory.hpp \
    src/files/FileActions.hpp

SOURCES += \
    src/main.cpp \
    src/core/DaemonServer.cpp \
    src/core/LayerShellWindow.cpp \
    src/core/SingleInstance.cpp \
    src/core/RelativePointer.cpp \
    src/ui/ShellBridge.cpp \
    src/ui/ThemeManager.cpp \
    src/ui/IconProviders.cpp \
    src/audio/AudioEngine.cpp \
    src/audio/SoundConfig.cpp \
    src/search/AppIndexer.cpp \
    src/search/KRunnerActions.cpp \
    src/search/CalcConverter.cpp \
    src/run/CommandAnalyzer.cpp \
    src/run/CommandRunner.cpp \
    src/run/CommandHistory.cpp \
    src/files/FileActions.cpp
