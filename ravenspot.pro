# ==============================================================================
# RavenSpot - Native C++20 Desktop Launcher & Resident Daemon
# Build Configuration (QMake6 / Qt 6.12.0)
#
# Linux (KDE Plasma 6 / Wayland) and Windows (Qt 6 + MSVC) share Menu.qml and
# the portable C++ core; platform-specific parts live in *_linux.cpp / *_win.cpp
# or behind Q_OS_WIN guards. The !win32 scope below is the original Linux build.
# ==============================================================================

QT += core gui quick qml multimedia network
CONFIG += c++20 release

# Include project source roots
INCLUDEPATH += src

# Target executable
TARGET = ravenspot
TEMPLATE = app

# ── Shared sources (both platforms) ───────────────────────────────────────────
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
    src/search/AppClassify.hpp \
    src/ui/IconAlias.hpp \
    src/search/CalcConverter.hpp \
    src/run/CommandAnalyzer.hpp \
    src/run/CommandRunner.hpp \
    src/run/CommandHistory.hpp \
    src/files/FileActions.hpp \
    src/pc/ClipboardHistory.hpp \
    src/pc/ProcessList.hpp \
    src/pc/RecentFiles.hpp \
    src/pc/EmojiIndex.hpp \
    src/net/VideoDownload.hpp

SOURCES += \
    src/main.cpp \
    src/core/DaemonServer.cpp \
    src/ui/ShellBridge.cpp \
    src/ui/ThemeManager.cpp \
    src/ui/IconProviders.cpp \
    src/audio/AudioEngine.cpp \
    src/audio/SoundConfig.cpp \
    src/search/AppIndexer.cpp \
    src/search/AppClassify.cpp \
    src/ui/IconAlias.cpp \
    src/search/CalcConverter.cpp \
    src/run/CommandAnalyzer.cpp \
    src/run/CommandRunner.cpp \
    src/run/CommandHistory.cpp \
    src/files/FileActions.cpp \
    src/pc/ClipboardHistory.cpp \
    src/pc/ProcessList.cpp \
    src/pc/RecentFiles.cpp \
    src/pc/EmojiIndex.cpp \
    src/net/VideoDownload.cpp

# ── Linux: KDE Plasma 6 / Wayland ─────────────────────────────────────────────
!win32 {
    QT += dbus
    CONFIG += link_pkgconfig

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

    # Compiler Optimization & Strict Warnings
    # Note: -mno-direct-extern-access and -fPIC are required for GCC on Arch/CachyOS
    QMAKE_CXXFLAGS += -std=c++20 -O2 -pipe -Wall -Wextra -Wpedantic -mno-direct-extern-access -fPIC
    QMAKE_LFLAGS += -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now

    HEADERS += \
        src/core/RelativePointer.hpp \
        src/search/KRunnerActions.hpp

    SOURCES += \
        src/core/LayerShellWindow.cpp \
        src/core/SingleInstance.cpp \
        src/core/RelativePointer.cpp \
        src/ui/ShellBridge_linux.cpp \
        src/search/KRunnerActions.cpp
}

# ── Windows 10/11: Qt 6 + MSVC ────────────────────────────────────────────────
win32 {
    # GUI subsystem (no console window); CLI replies go to the parent console if any
    CONFIG -= console
    CONFIG += windows
    # The repo root has a VERSION file: on a case-insensitive file system it
    # would shadow the C++20 <version> header if the source dir were on -I.
    CONFIG += no_include_pwd

    msvc {
        QMAKE_CXXFLAGS += /utf-8 /permissive- /Zc:__cplusplus /W3
        DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
    }

    LIBS += -luser32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lpowrprof -ldwmapi -lpsapi -lgdi32

    HEADERS += \
        src/core/GlobalHotkey_win.hpp \
        src/core/WinUtil.hpp

    SOURCES += \
        src/core/LayerShellWindow_win.cpp \
        src/core/SingleInstance_win.cpp \
        src/core/GlobalHotkey_win.cpp \
        src/core/WinUtil.cpp \
        src/ui/ShellBridge_win.cpp \
        src/search/AppIndexer_win.cpp

    RC_ICONS = assets/ravenspot.ico
    VERSION = 0.3.0
    QMAKE_TARGET_DESCRIPTION = RavenSpot launcher
}
