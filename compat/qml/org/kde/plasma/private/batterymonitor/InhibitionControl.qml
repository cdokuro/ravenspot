import QtQml

// Windows stand-in for KDE's batterymonitor InhibitionControl so the shared
// Menu.qml loads unchanged. Sleep blocking on Windows goes through
// Shell.setSleepInhibited (PowerCreateRequest); nothing else inhibits it here.
// Only added to the QML import path on Windows (see main.cpp).
QtObject {
    property bool isManuallyInhibited: false
}
