#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>
#include <thread>
#include <chrono>

#include "core/LayerShellWindow.hpp"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::cout << "Testing Edge Case: Early focus loss during 350ms grace period\n";

    ravenspot::LayerShellWindow lsw;
    QQuickWindow *win = lsw.window();

    // Show window
    lsw.showWindow();
    app.processEvents();
    std::cout << "Window shown. isVisible: " << lsw.isVisible() << "\n";

    // Simulate window gaining focus
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    app.processEvents();

    // Now simulate focus loss at t=50ms (e.g. user clicked away immediately)
    std::cout << "Simulating user click away at t=50ms (window loses focus)...\n";
    // We emit focusWindowChanged(nullptr)
    emit qGuiApp->focusWindowChanged(nullptr);
    app.processEvents();

    std::cout << "At t=50ms: isVisible = " << lsw.isVisible() << " (Expected: still visible due to grace period)\n";

    // Wait until 500ms has elapsed (grace timer was 350ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    app.processEvents();

    std::cout << "At t=550ms: isVisible = " << lsw.isVisible() << "\n";

    if (lsw.isVisible()) {
        std::cout << "[FINDING CONFIRMED] Window is STILL VISIBLE at t=550ms even though focus was lost!\n"
                  << "Because m_canCloseOnInactive was never set to true while window was inactive,\n"
                  << "the window is now stuck in an un-closable state!\n";
    } else {
        std::cout << "[OK] Window auto-hid successfully.\n";
    }

    return 0;
}
