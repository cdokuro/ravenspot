# Komut çalıştırıcı birim testleri: cd tests/cpp && qmake6 && make && ./cmdtests
QT += core testlib network
QT -= gui
CONFIG += c++20 console testcase
CONFIG -= app_bundle
TARGET = cmdtests
INCLUDEPATH += ../../src
SOURCES += main.cpp tst_commandanalyzer.cpp tst_commandhistory.cpp tst_fileactions.cpp tst_clipboardhistory.cpp tst_processlist.cpp tst_recentfiles.cpp tst_emojiindex.cpp tst_videodownload.cpp \
    ../../src/pc/EmojiIndex.cpp \
    ../../src/pc/RecentFiles.cpp \
    ../../src/pc/ProcessList.cpp \
    ../../src/pc/ClipboardHistory.cpp \
    ../../src/files/FileActions.cpp \
    ../../src/run/CommandHistory.cpp \
    ../../src/run/CommandRunner.cpp \
    ../../src/run/CommandAnalyzer.cpp \
    ../../src/net/VideoDownload.cpp
HEADERS += ../../src/run/CommandAnalyzer.hpp ../../src/run/CommandRunner.hpp ../../src/run/CommandHistory.hpp ../../src/files/FileActions.hpp ../../src/pc/ClipboardHistory.hpp ../../src/pc/ProcessList.hpp ../../src/pc/RecentFiles.hpp ../../src/pc/EmojiIndex.hpp ../../src/net/VideoDownload.hpp
DEFINES += SRCDIR=\\\"$$PWD\\\"

# bash-based runner tests on Linux; PowerShell/Job-object and hotkey tests on Windows
!win32: SOURCES += tst_commandrunner.cpp
win32 {
    SOURCES += tst_winplatform.cpp ../../src/core/GlobalHotkey_win.cpp
    HEADERS += ../../src/core/GlobalHotkey_win.hpp
    LIBS += -luser32
    msvc: QMAKE_CXXFLAGS += /utf-8
}
