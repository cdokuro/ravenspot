// Windows replacement for the Wayland layer-shell overlay: a frameless,
// always-on-top tool window (no taskbar button) centred on the screen under
// the cursor. Dragging uses the native move loop (QWindow::startSystemMove).
#include "LayerShellWindow.hpp"
#include "core/WinUtil.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QRegion>
#include <algorithm>

namespace ravenspot {

LayerShellWindow::LayerShellWindow(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    initWindow();
}

LayerShellWindow::~LayerShellWindow() {
    if (m_ownsWindow && m_window) delete m_window.data();
}

void LayerShellWindow::attachWindow(QQuickWindow *window) {
    if (!window) return;
    if (m_ownsWindow && m_window) delete m_window.data();
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
    m_window->setColor(Qt::transparent);
    // Qt::Tool: no taskbar button / Alt+Tab entry, like a launcher overlay
    m_window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    m_window->create();
    applyNativeStyle();
    updateBlurRegion(m_cardHeight);

    m_graceTimer.setInterval(350);
    m_graceTimer.setSingleShot(true);
    connect(&m_graceTimer, &QTimer::timeout, this, &LayerShellWindow::onGraceTimerTriggered);
    connect(m_window.data(), &QQuickWindow::activeChanged, this, &LayerShellWindow::onWindowActiveChanged);
    // Native move loop: keep our stored position in sync with where the user put it
    connect(m_window.data(), &QWindow::xChanged, this, [this]() { if (m_isDragging) syncPositionFromWindow(); });
    connect(m_window.data(), &QWindow::yChanged, this, [this]() { if (m_isDragging) syncPositionFromWindow(); });
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, &LayerShellWindow::onFocusWindowChanged);
    }
}

void LayerShellWindow::applyNativeStyle() {
    win::applyWindowStyle(m_window.data());
}

void LayerShellWindow::setCardHeight(int height) {
    if (height <= 0 || height == m_cardHeight) return;
    m_cardHeight = height;
    updateBlurRegion(m_cardHeight);
}

void LayerShellWindow::updateBlurRegion(int cardHeight) {
    if (!m_window) return;
    // Card 495px wide inside the 543px window (24px glow margin). Clicks outside
    // the card fall through to the window underneath, as on Wayland.
    const int margin = 24;
    const int cardWidth = 495;
    m_window->setMask(QRegion(QRect(margin, margin, cardWidth, cardHeight)));
}

int LayerShellWindow::visibleHeight() const {
    return m_cardHeight + 48;
}

void LayerShellWindow::showOnActiveScreen() {
    if (!m_window) return;

    QScreen *target = QGuiApplication::screenAt(QCursor::pos());
    if (!target) target = QGuiApplication::primaryScreen();
    if (target) {
        if (m_lastScreen != target) {
            m_lastScreen = target;
            m_hasCustomPosition = false; // re-centre on a new screen
            m_window->setScreen(target);
        }
        const QRect geom = target->availableGeometry(); // above the taskbar
        const int winW = m_window->width() > 0 ? m_window->width() : 543;
        if (!m_hasCustomPosition) {
            m_posX = (geom.width() - winW) / 2;
            m_posY = std::max(0, (geom.height() - visibleHeight()) / 2);
        }
        m_window->setPosition(geom.x() + m_posX, geom.y() + m_posY);
        emit screenChanged(target);
    }

    m_canCloseOnInactive = false;
    m_graceTimer.stop();
    m_window->setVisible(true);
    m_window->requestActivate();
    win::forceForeground(m_window.data());
    m_graceTimer.start();

    emit visibilityChanged(true);
}

void LayerShellWindow::hideWindow() {
    if (!m_window || !m_window->isVisible()) return;
    m_graceTimer.stop();
    m_canCloseOnInactive = false;
    m_isDragging = false;
    m_window->setVisible(false);
    emit visibilityChanged(false);
}

void LayerShellWindow::toggleWindow() {
    if (isVisible()) hideWindow();
    else showOnActiveScreen();
}

bool LayerShellWindow::isVisible() const {
    return m_window && m_window->isVisible();
}

QScreen *LayerShellWindow::activeScreen() const {
    return m_window ? m_window->screen() : nullptr;
}

void LayerShellWindow::beginDrag(double initialDx, double initialDy) {
    Q_UNUSED(initialDx)
    Q_UNUSED(initialDy)
    if (!m_window) return;
    m_isDragging = true;
    // Windows' own move loop: smooth at any polling rate, crosses monitors,
    // no feedback between window movement and QML-local mouse coordinates.
    if (!m_window->startSystemMove()) m_isDragging = false;
}

QPoint LayerShellWindow::dragBy(double deltaX, double deltaY) {
    // The native move loop owns the window while dragging; QML deltas are ignored.
    Q_UNUSED(deltaX)
    Q_UNUSED(deltaY)
    return QPoint(0, 0);
}

void LayerShellWindow::updateDrag(double localX, double localY) {
    Q_UNUSED(localX)
    Q_UNUSED(localY)
}

void LayerShellWindow::endDrag() {
    if (!m_isDragging) return;
    syncPositionFromWindow();
    m_isDragging = false;
}

void LayerShellWindow::syncPositionFromWindow() {
    if (!m_window) return;
    QScreen *screen = m_window->screen();
    if (!screen) return;
    const QRect geom = screen->availableGeometry();
    m_lastScreen = screen;
    m_posX = m_window->x() - geom.x();
    m_posY = m_window->y() - geom.y();
    m_hasCustomPosition = true;
}

void LayerShellWindow::setWindowPosition(int x, int y) {
    m_posX = x;
    m_posY = y;
    m_hasCustomPosition = true;
    if (m_window && m_window->screen()) {
        const QRect geom = m_window->screen()->availableGeometry();
        m_window->setPosition(geom.x() + m_posX, geom.y() + m_posY);
    }
}

void LayerShellWindow::onGraceTimerTriggered() {
    m_canCloseOnInactive = true;
    if (m_window && !m_window->isActive() && !m_isDragging) hideWindow();
}

void LayerShellWindow::onWindowActiveChanged() {
    if (!m_window) return;
    if (m_window->isActive()) {
        m_graceTimer.start();
    } else if (m_canCloseOnInactive && m_window->isVisible() && !m_isDragging) {
        hideWindow();
    }
}

void LayerShellWindow::onFocusWindowChanged(QWindow *focusWindow) {
    if (!m_window || !m_window->isVisible() || !m_canCloseOnInactive || m_isDragging) return;
    // Popups (context menu) live in the same QQuickWindow; a null focus window
    // means another application took focus.
    if (focusWindow != m_window.data()) hideWindow();
}

} // namespace ravenspot
