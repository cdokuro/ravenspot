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
        m_window->resize(543, 648);
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
    m_layerWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
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

    // Card width is 495px centered in 543px container (24px padding on each side)
    const int margin = 24;
    const int cardWidth = 495;
    const QRegion blurRegion(QRect(margin, margin, cardWidth, cardHeight));

    KWindowEffects::enableBlurBehind(m_window.data(), true, blurRegion);
    KWindowEffects::enableBackgroundContrast(m_window.data(), true, 1.0, 1.0, 1.0, blurRegion);
}

QScreen* LayerShellWindow::resolveActiveScreen() {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return nullptr;
    if (screens.size() == 1) return screens.first();

    // 1. Fast check: Screen under cursor (0ms in-memory calculation)
    if (QScreen *cursorScreen = QGuiApplication::screenAt(QCursor::pos())) {
        return cursorScreen;
    }

    // 2. Query KWin activeOutputName via D-Bus
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

    // 3. Fallback: Primary screen
    return QGuiApplication::primaryScreen();
}

void LayerShellWindow::showOnActiveScreen() {
    if (!m_window) return;

    QScreen *target = resolveActiveScreen();
    if (target) {
        if (m_lastScreen != target) {
            m_lastScreen = target;
            m_hasCustomPosition = false; // re-center on new screen
            if (m_layerWindow) {
                m_layerWindow->setScreen(target);
            }
            m_window->setScreen(target);
        }

        const QRect geom = target->geometry();
        const int winW = m_window->width() > 0 ? m_window->width() : 543;
        const int winH = m_window->height() > 0 ? m_window->height() : 648;

        if (!m_hasCustomPosition) {
            m_posX = (geom.width() - winW) / 2;
            m_posY = (geom.height() - winH) / 2;
        }

        if (m_layerWindow) {
            m_layerWindow->setMargins(QMargins(m_posX, m_posY, 0, 0));
        }

        // Positioning for X11 sessions
        if (!m_isWayland) {
            m_window->setPosition(geom.x() + m_posX, geom.y() + m_posY);
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

void LayerShellWindow::beginDrag(double localX, double localY) {
    Q_UNUSED(localX);
    Q_UNUSED(localY);
    m_dragStartWinPos = QPoint(m_posX, m_posY);
    m_accumulatedDeltaX = 0.0;
    m_accumulatedDeltaY = 0.0;
    m_isDragging = true;
}

QPoint LayerShellWindow::dragBy(double deltaX, double deltaY) {
    if (!m_isDragging) return QPoint(0, 0);

    m_accumulatedDeltaX += deltaX;
    m_accumulatedDeltaY += deltaY;

    const int intDx = static_cast<int>(std::round(m_accumulatedDeltaX));
    const int intDy = static_cast<int>(std::round(m_accumulatedDeltaY));

    if (intDx == 0 && intDy == 0) return QPoint(0, 0);

    m_accumulatedDeltaX -= intDx;
    m_accumulatedDeltaY -= intDy;

    QScreen *screen = m_window ? m_window->screen() : nullptr;
    if (!screen) screen = resolveActiveScreen();

    const int maxW = screen ? screen->geometry().width() : 2560;
    const int maxH = screen ? screen->geometry().height() : 1440;
    const int winW = m_window ? m_window->width() : 543;
    const int winH = m_window ? m_window->height() : 648;

    const int newX = std::clamp(m_posX + intDx, 0, std::max(0, maxW - winW));
    const int newY = std::clamp(m_posY + intDy, 0, std::max(0, maxH - winH));

    const int appliedDx = newX - m_posX;
    const int appliedDy = newY - m_posY;

    if (appliedDx == 0 && appliedDy == 0) return QPoint(0, 0);

    m_posX = newX;
    m_posY = newY;
    m_hasCustomPosition = true;

    if (m_layerWindow) {
        m_layerWindow->setMargins(QMargins(m_posX, m_posY, 0, 0));
    }
    if (m_window) {
        m_window->requestUpdate();
    }
    if (!m_isWayland && m_window && screen) {
        m_window->setPosition(screen->geometry().x() + m_posX, screen->geometry().y() + m_posY);
    }

    return QPoint(appliedDx, appliedDy);
}

void LayerShellWindow::updateDrag(double localX, double localY) {
    Q_UNUSED(localX);
    Q_UNUSED(localY);
}

void LayerShellWindow::endDrag() {
    m_isDragging = false;
    m_accumulatedDeltaX = 0.0;
    m_accumulatedDeltaY = 0.0;
}

void LayerShellWindow::setWindowPosition(int x, int y) {
    m_posX = x;
    m_posY = y;
    m_hasCustomPosition = true;
    if (m_layerWindow) {
        m_layerWindow->setMargins(QMargins(m_posX, m_posY, 0, 0));
    }
    if (m_window) {
        m_window->requestUpdate();
    }
    if (!m_isWayland && m_window && m_window->screen()) {
        m_window->setPosition(m_window->screen()->geometry().x() + m_posX, m_window->screen()->geometry().y() + m_posY);
    }
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
