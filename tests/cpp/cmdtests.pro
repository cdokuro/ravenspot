# Komut çalıştırıcı birim testleri: cd tests/cpp && qmake6 && make && ./cmdtests
QT += core testlib
QT -= gui
CONFIG += c++20 console testcase
CONFIG -= app_bundle
TARGET = cmdtests
INCLUDEPATH += ../../src
SOURCES += main.cpp tst_commandanalyzer.cpp \
    ../../src/run/CommandAnalyzer.cpp
HEADERS += ../../src/run/CommandAnalyzer.hpp
