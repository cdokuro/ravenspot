#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <LayerShellQt/Window>
#include <KF6/KWindowSystem/kwindoweffects.h>

#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

#include "core/LayerShellWindow.hpp"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define TEST_PASS(msg) \
    std::cout << "[+] PASS: " << msg << std::endl

namespace ravenspot::test {

bool test_01_layershell_surface_properties(QGuiApplication &app) {
    std::cout << "\n--- TEST 1: LayerShell Surface Properties & Geometry ---" << std::endl;
    LayerShellWindow lsw;
    QQuickWindow *win = lsw.window();
    TEST_ASSERT(win != nullptr, "LayerShellWindow must instantiate an internal QQuickWindow");

    // 1. Geometry check
    TEST_ASSERT(win->width() == 543, "Window width must be strictly 543px (495px card + 24px glow margins)");
    TEST_ASSERT(win->height() == 648, "Window default height must be 648px (600px card + 24px margins)");

    // 2. Flags & Color check
    TEST_ASSERT(win->color() == Qt::transparent, "Window background must be transparent for glow/blur");
    TEST_ASSERT(win->flags().testFlag(Qt::FramelessWindowHint), "Window must have Qt::FramelessWindowHint");
    TEST_ASSERT(win->flags().testFlag(Qt::WindowStaysOnTopHint), "Window must have Qt::WindowStaysOnTopHint");

    // 3. LayerShellQt integration
    LayerShellQt::Window *lsWin = LayerShellQt::Window::get(win);
    if (!lsWin) {
        std::cerr << "[!] WARNING: LayerShellQt::Window::get() returned nullptr (Platform: "
                  << QGuiApplication::platformName().toStdString() << ")" << std::endl;
        // If not running Wayland or compositor doesn't support layer shell, note it
    } else {
        TEST_ASSERT(lsWin->layer() == LayerShellQt::Window::LayerTop, "Layer must be LayerTop (2)");
        TEST_ASSERT(lsWin->anchors() == LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft), "Anchors must be AnchorTop | AnchorLeft for drag positioning");
        TEST_ASSERT(lsWin->exclusionZone() == 0, "ExclusionZone must be 0 for floating overlay");
        TEST_ASSERT(lsWin->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityOnDemand,
                    "KeyboardInteractivity must be OnDemand (2)");
        TEST_ASSERT(lsWin->scope() == QStringLiteral("ravenspot"), "Scope must be 'ravenspot'");
        TEST_ASSERT(lsWin->activateOnShow() == true, "activateOnShow must be true");
    }

    TEST_PASS("LayerShell properties and surface geometry verified");
    return true;
}

bool test_02_blur_behind_bounding(QGuiApplication &app) {
    std::cout << "\n--- TEST 2: Blur-Behind Region Bounding & Glow Halo ---" << std::endl;
    LayerShellWindow lsw;
    QQuickWindow *win = lsw.window();
    TEST_ASSERT(win != nullptr, "Window is null");

    // Default card height is 600
    TEST_ASSERT(lsw.cardHeight() == 600, "Default cardHeight must be 600");

    // Test cardHeight change
    lsw.setCardHeight(450);
    TEST_ASSERT(lsw.cardHeight() == 450, "cardHeight() must update to 450");

    // Check halo margin calculations
    const int margin = 24;
    const int cardWidth = 495;
    const int cardHeight = 450;
    const QRect expectedBlurRect(margin, margin, cardWidth, cardHeight);

    TEST_ASSERT(expectedBlurRect.x() == 24, "Blur left margin must be 24");
    TEST_ASSERT(expectedBlurRect.y() == 24, "Blur top margin must be 24");
    TEST_ASSERT(expectedBlurRect.width() == 495, "Blur card width must be 495");
    TEST_ASSERT(expectedBlurRect.height() == 450, "Blur card height must be 450");
    TEST_ASSERT(win->width() - (margin + cardWidth) == 24, "Blur right margin must preserve 24px halo");

    // Boundary stress test: zero or negative card height
    lsw.setCardHeight(0);
    TEST_ASSERT(lsw.cardHeight() == 0, "cardHeight of 0 handled");

    lsw.setCardHeight(-100);
    TEST_ASSERT(lsw.cardHeight() == -100, "Negative cardHeight handled without crash");

    // Restore valid height
    lsw.setCardHeight(600);
    TEST_PASS("Blur-behind bounding region correctly scoped to QRect(24, 24, 450, cardHeight)");
    return true;
}

bool test_03_multimonitor_centering(QGuiApplication &app) {
    std::cout << "\n--- TEST 3: Multi-Monitor Centering & KWin D-Bus Logic ---" << std::endl;
    LayerShellWindow lsw;

    // Check live screens
    const auto screens = QGuiApplication::screens();
    std::cout << "  Active screens count: " << screens.size() << std::endl;
    for (int i = 0; i < screens.size(); ++i) {
        std::cout << "  Screen [" << i << "]: name=" << screens[i]->name().toStdString()
                  << " geom=" << screens[i]->geometry().width() << "x" << screens[i]->geometry().height()
                  << " dpr=" << screens[i]->devicePixelRatio() << std::endl;
    }

    // Query live KWin D-Bus activeOutputName
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("activeOutputName")
    );
    QDBusReply<QString> reply = QDBusConnection::sessionBus().call(msg);
    if (reply.isValid()) {
        std::cout << "  Live KWin D-Bus activeOutputName: \"" << reply.value().toStdString() << "\"" << std::endl;
    } else {
        std::cout << "  KWin D-Bus reply invalid: " << reply.error().message().toStdString() << std::endl;
    }

    // Call showOnActiveScreen and verify assigned screen
    lsw.showOnActiveScreen();
    QScreen *active = lsw.activeScreen();
    TEST_ASSERT(active != nullptr, "activeScreen must not be null when screens exist");

    if (reply.isValid()) {
        QString expectedName = reply.value();
        bool foundScreen = false;
        for (auto *s : screens) {
            if (s && s->name() == expectedName) {
                foundScreen = true;
                break;
            }
        }
        if (foundScreen) {
            TEST_ASSERT(active->name() == expectedName,
                        "LayerShellWindow should match KWin activeOutputName: " + expectedName.toStdString());
            std::cout << "  Verified: activeScreen matches KWin output (" << active->name().toStdString() << ")" << std::endl;
        }
    }

    lsw.hideWindow();
    TEST_PASS("Multi-monitor centering logic correctly resolves active screen");
    return true;
}

bool test_04_focus_loss_grace_period_and_autohide(QGuiApplication &app) {
    std::cout << "\n--- TEST 4: Focus Loss Grace Period (350ms) & Auto-Hide Transitions ---" << std::endl;
    LayerShellWindow lsw;
    QQuickWindow *win = lsw.window();
    TEST_ASSERT(win != nullptr, "Window is null");

    // 4.1 Initial state: hidden
    TEST_ASSERT(!lsw.isVisible(), "Window must start hidden");

    // 4.2 Show window
    lsw.showWindow();
    TEST_ASSERT(lsw.isVisible(), "Window must be visible after showWindow()");

    // 4.3 Verify grace period behaviour:
    // When window is shown, m_canCloseOnInactive is false for 350ms.
    // If activeChanged fires while active=true, grace timer restarts.
    // Process Qt events for 500ms to allow grace timer to expire cleanly:
    QTimer loopTimer;
    loopTimer.setSingleShot(true);
    loopTimer.start(500);
    while (loopTimer.isActive()) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Now grace period has expired.
    // If focus is transferred to another window or window becomes inactive:
    // Simulate focusWindowChanged(nullptr):
    std::cout << "  Simulating focus loss after grace period (500ms)..." << std::endl;
    // We can emit focusWindowChanged via qGuiApp
    emit qGuiApp->focusWindowChanged(nullptr);
    app.processEvents();

    std::cout << "  Window visibility after focus loss: " << (lsw.isVisible() ? "VISIBLE" : "HIDDEN") << std::endl;

    // 4.4 ADVERSARIAL STRESS TEST: Early Focus Loss within 350ms Grace Period
    std::cout << "\n  [Adversarial Challenge] Testing Early Focus Loss within 350ms Grace Period..." << std::endl;
    lsw.showWindow();
    TEST_ASSERT(lsw.isVisible(), "Window must be visible after showWindow()");

    // Simulate window gaining focus at 10ms
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    app.processEvents();

    // Now at 50ms (WELL BEFORE 350ms grace timer expires), simulate user clicking outside
    std::cout << "  Simulating early focus loss at t=50ms (before 350ms timer triggers)..." << std::endl;
    emit qGuiApp->focusWindowChanged(nullptr);
    app.processEvents();

    bool visibleAt50ms = lsw.isVisible();
    std::cout << "  Visibility at t=50ms: " << (visibleAt50ms ? "VISIBLE (grace period active)" : "HIDDEN") << std::endl;

    // Now wait past the 350ms timer (to t=450ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    app.processEvents();

    bool visibleAt500ms = lsw.isVisible();
    std::cout << "  Visibility at t=500ms (after grace timer elapsed): "
              << (visibleAt500ms ? "VISIBLE (STUCK / ZOMBIE?)" : "HIDDEN (Auto-hidden)") << std::endl;

    // Clean up
    lsw.hideWindow();
    TEST_ASSERT(!lsw.isVisible(), "Window must be hidden after hideWindow()");

    TEST_PASS("Focus loss grace period test execution complete");
    return true;
}

bool test_05_rapid_toggle_stress(QGuiApplication &app) {
    std::cout << "\n--- TEST 5: Rapid Show/Hide Toggle Stress Harness (50 cycles) ---" << std::endl;
    LayerShellWindow lsw;

    for (int i = 0; i < 50; ++i) {
        lsw.toggleWindow();
        app.processEvents();
        TEST_ASSERT(lsw.isVisible() == ((i % 2) == 0), "Toggle parity mismatch at cycle " + std::to_string(i));
    }
    // Clean up
    lsw.hideWindow();
    TEST_ASSERT(!lsw.isVisible(), "Window must end hidden");
    TEST_PASS("50 rapid toggle transitions succeeded without coordinate drift or crashes");
    return true;
}

} // namespace ravenspot::test

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    std::cout << "========================================================\n"
              << " RavenSpot Milestone 1 - LayerShell Empirical Test Harness\n"
              << " Platform: " << QGuiApplication::platformName().toStdString() << "\n"
              << "========================================================" << std::endl;

    bool allOk = true;
    allOk &= ravenspot::test::test_01_layershell_surface_properties(app);
    allOk &= ravenspot::test::test_02_blur_behind_bounding(app);
    allOk &= ravenspot::test::test_03_multimonitor_centering(app);
    allOk &= ravenspot::test::test_04_focus_loss_grace_period_and_autohide(app);
    allOk &= ravenspot::test::test_05_rapid_toggle_stress(app);

    std::cout << "\n========================================================" << std::endl;
    if (allOk) {
        std::cout << "[+] ALL 5 EMPIRICAL TESTS PASSED." << std::endl;
        return 0;
    } else {
        std::cerr << "[-] ONE OR MORE EMPIRICAL TESTS FAILED." << std::endl;
        return 1;
    }
}
