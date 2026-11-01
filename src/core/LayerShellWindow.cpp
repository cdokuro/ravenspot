#include "LayerShellWindow.hpp"

#include <QGuiApplication>
#include <QCursor>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <KF6/KWindowSystem/kwindoweffects.h>

namespace ravenspot {

LayerShellWindow::LayerShellWindow(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    initWindow();
}

LayerShellWindow::~LayerShellWindow() {
    if (m_ownsWindow && m_window) {
        delete m_window.data();
    }
}

void LayerShellWindow::attachWindow(QQuickWindow *window) {
    if (!window) return;
    if (m_ownsWindow && m_window) {
        delete m_window.data();
    }
    m_window = window;
    m_ownsWindow = false;
    initWindow();
}

void LayerShellWindow::initWindow() {
    if (!m_window) {
        m_ownsWindow = true;
        m_window = new QQuickWindow();
        m_window->resize(498, 648);
    }

    m_isWayland = QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);

    m_window->setColor(Qt::transparent);
    m_window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_window->create();

    initLayerShell();
    setupBlur();

    m_graceTimer.setInterval(350);
    m_graceTimer.setSingleShot(true);
    connect(&m_graceTimer, &QTimer::timeout, this, &LayerShellWindow::onGraceTimerTriggered);

    connect(m_window.data(), &QQuickWindow::activeChanged, this, &LayerShellWindow::onWindowActiveChanged);
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, &LayerShellWindow::onFocusWindowChanged);
    }
}

void LayerShellWindow::initLayerShell() {
    if (!m_window) return;

    m_layerWindow = LayerShellQt::Window::get(m_window.data());
    if (!m_layerWindow) {
        qDebug() << "LayerShellQt: Layer shell not available on current platform";
        return;
    }

    m_layerWindow->setLayer(LayerShellQt::Window::LayerTop);
    m_layerWindow->setScope(QStringLiteral("ravenspot"));
    m_layerWindow->setAnchors(LayerShellQt::Window::AnchorNone); // Centered
    m_layerWindow->setExclusiveZone(0);                           // Floating overlay
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    m_layerWindow->setActivateOnShow(true);
    m_layerWindow->setWantsToBeOnActiveScreen(true);
}

void LayerShellWindow::setupBlur() {
    updateBlurRegion(m_cardHeight);
}

void LayerShellWindow::setCardHeight(int height) {
    m_cardHeight = height;
    updateBlurRegion(m_cardHeight);
}

void LayerShellWindow::updateBlurRegion(int cardHeight) {
    if (!m_window) return;

    // Card width is 450px centered in 498px container (24px padding on each side)
    const int margin = 24;
    const int cardWidth = 450;
    const QRegion blurRegion(QRect(margin, margin, cardWidth, cardHeight));

    KWindowEffects::enableBlurBehind(m_window.data(), true, blurRegion);
    KWindowEffects::enableBackgroundContrast(m_window.data(), true, 1.0, 1.0, 1.0, blurRegion);
}

QScreen* LayerShellWindow::resolveActiveScreen() {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return nullptr;

    // 1. Query KWin activeOutputName via D-Bus
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("activeOutputName")
    );

    QDBusReply<QString> reply = QDBusConnection::sessionBus().call(msg);
    if (reply.isValid()) {
        const QString activeName = reply.value();
        for (QScreen *s : screens) {
            if (s && s->name() == activeName) {
                return s;
            }
        }
    }

    // 2. Fallback: Cursor position
    if (QScreen *cursorScreen = QGuiApplication::screenAt(QCursor::pos())) {
        return cursorScreen;
    }

    // 3. Fallback: Primary screen
    return QGuiApplication::primaryScreen();
}

void LayerShellWindow::showOnActiveScreen() {
    if (!m_window) return;

    QScreen *target = resolveActiveScreen();
    if (target) {
        if (m_layerWindow) {
            m_layerWindow->setScreen(target);
        }
        m_window->setScreen(target);

        // Positioning for X11 sessions
        if (!m_isWayland) {
            const QRect geom = target->geometry();
            const int x = geom.x() + (geom.width() - m_window->width()) / 2;
            const int y = geom.y() + (geom.height() - m_window->height()) / 2;
            m_window->setPosition(x, y);
        }
        emit screenChanged(target);
    }

    m_canCloseOnInactive = false;
    m_graceTimer.stop();

    m_window->setVisible(true);
    m_window->requestActivate();
    m_graceTimer.start(); // 350ms window activation grace period

    emit visibilityChanged(true);
}

void LayerShellWindow::hideWindow() {
    if (!m_window || !m_window->isVisible()) return;

    m_graceTimer.stop();
    m_canCloseOnInactive = false;
    m_window->setVisible(false);

    emit visibilityChanged(false);
}

void LayerShellWindow::toggleWindow() {
    if (isVisible()) {
        hideWindow();
    } else {
        showOnActiveScreen();
    }
}

bool LayerShellWindow::isVisible() const {
    return m_window && m_window->isVisible();
}

QScreen* LayerShellWindow::activeScreen() const {
    return m_window ? m_window->screen() : nullptr;
}

void LayerShellWindow::onGraceTimerTriggered() {
    m_canCloseOnInactive = true;
    if (m_window && !m_window->isActive()) {
        hideWindow();
    }
}

void LayerShellWindow::onWindowActiveChanged() {
    if (!m_window) return;

    if (m_window->isActive()) {
        m_graceTimer.start();
    } else if (m_canCloseOnInactive && m_window->isVisible()) {
        hideWindow();
    }
}

void LayerShellWindow::onFocusWindowChanged(QWindow *focusWindow) {
    if (!m_window || !m_window->isVisible() || !m_canCloseOnInactive) return;

    if (focusWindow != m_window.data()) {
        hideWindow();
    }
}

} // namespace ravenspot
