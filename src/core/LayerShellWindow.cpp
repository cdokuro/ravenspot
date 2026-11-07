#include "LayerShellWindow.hpp"

#include <QGuiApplication>
#include <QCursor>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <algorithm>
#include <cmath>
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

    m_postSwitchIdle.setInterval(1500);
    m_postSwitchIdle.setSingleShot(true);
    connect(&m_postSwitchIdle, &QTimer::timeout, this, [this]() {
        if (m_isDragging && m_dragSwitched) {
            qWarning() << "RavenSpot drag: no motion 1.5s after output switch -> ending drag";
            endDrag();
        }
    });

    m_switchGuard.setInterval(800);
    m_switchGuard.setSingleShot(true);
    connect(&m_switchGuard, &QTimer::timeout, this, [this]() { m_switchingScreen = false; });

    m_graceTimer.setInterval(350);
    m_graceTimer.setSingleShot(true);
    connect(&m_graceTimer, &QTimer::timeout, this, &LayerShellWindow::onGraceTimerTriggered);

    connect(m_window.data(), &QQuickWindow::activeChanged, this, &LayerShellWindow::onWindowActiveChanged);
    connect(m_window.data(), &QQuickWindow::afterAnimating, this, &LayerShellWindow::flushPendingMove);
    connect(&m_stallProbe, &QTimer::timeout, this, [this]() {
        const qint64 now = m_dragClock.elapsed();
        const qint64 stall = now - m_lastProbeMs;
        if (m_lastProbeMs > 0 && stall > 20) {
            ++m_stallsOver20;
            if (m_stallLog.size() < 300) m_stallLog += QStringLiteral(" %1ms@%2").arg(stall).arg(m_lastProbeMs);
        }
        if (m_lastProbeMs > 0 && stall > m_maxStallMs) { m_maxStallMs = stall; m_maxStallAtMs = m_lastProbeMs; }
        m_lastProbeMs = now;
    });
    // Render thread'den: yalnızca sayaç (RAVENSPOT_PERF sürükleme istatistiği)
    connect(m_window.data(), &QQuickWindow::frameSwapped, this, [this]() {
        if (m_perfDrag) ++m_dragSwaps;
    }, Qt::DirectConnection);

    if (m_isWayland && !m_relativePointer) {
        m_relativePointer = new RelativePointer(this);
        connect(m_relativePointer, &RelativePointer::motion, this, &LayerShellWindow::onRelativeMotion);
        // Ekran değişiminde Qt bırakma olayını kaçırabilir; ham düğme olayı kesin kaynak.
        connect(m_relativePointer, &RelativePointer::leftButtonReleased, this, [this]() {
            if (!m_isDragging) return;
            if (m_dragSwitched) qInfo() << "RavenSpot drag: release received after output switch";
            endDrag();
        });
        connect(m_relativePointer, &RelativePointer::leftButtonPressed, this, [this]() {
            // Geçişten sonra yeni bir basış: bırakmayı kaçırmışız demektir.
            if (m_isDragging && m_dragSwitched) {
                qWarning() << "RavenSpot drag: new press while dragging after switch -> ending drag (missed release)";
                endDrag();
            }
        });
        (void)m_relativePointer->ensureReady(); // manager'ı açılışta bağla; ilk drag'de gecikme olmasın
    }
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
    if (height <= 0 || height == m_cardHeight) return;
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

    // Yüzey sabit boyutlu; kartın dışındaki şeffaf alan tıklamaları
    // alttaki pencereye geçirsin (Wayland: wl_surface input region).
    m_window->setMask(blurRegion);
}

int LayerShellWindow::visibleHeight() const {
    // Konumlandırma/sınırlama için görünen alan: kart + üst/alt 24px gölge payı.
    return m_cardHeight + 48;
}

QScreen* LayerShellWindow::resolveActiveScreen() {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return nullptr;
    if (screens.size() == 1) return screens.first();

    // X11: global imleç konumu geçerlidir, ekranı doğrudan buradan bul.
    // Wayland: istemciler global imleç konumunu göremez; QCursor::pos() hep
    // (0,0) döner ve screenAt() her zaman (0,0)'daki ekranı (DP-1) seçerdi.
    if (!m_isWayland) {
        if (QScreen *cursorScreen = QGuiApplication::screenAt(QCursor::pos())) {
            return cursorScreen;
        }
    }

    // KWin'in aktif çıkışı (odak/imleç politikasına göre). Süreç içinde ~60-150µs;
    // KWin yanıt vermezse hotkey'i dondurmamak için kısa zaman aşımı.
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("activeOutputName")
    );

    QDBusReply<QString> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 50);
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
        const int winH = visibleHeight();

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

void LayerShellWindow::beginDrag(double initialDx, double initialDy) {
    // Wayland'de konumdan bağımsız ham delta kaynağını tercih et.
    m_dragUsesRelativePointer = m_relativePointer && m_relativePointer->ensureReady();
    m_accumulatedDeltaX = 0.0;
    m_accumulatedDeltaY = 0.0;
    m_pendingDx = 0.0;
    m_pendingDy = 0.0;
    m_isDragging = true;
    m_dragSwitched = false;
    m_loggedFirstMotionAfterSwitch = false;
    m_dragVirtual = globalWindowPos();
    m_perfDrag = qEnvironmentVariableIsSet("RAVENSPOT_PERF");
    if (m_perfDrag) {
        m_dragClock.start();
        m_lastFlushMs = 0; m_maxFlushGapMs = 0;
        m_dragMotions = 0; m_dragFlushes = 0; m_dragSwaps = 0;
        m_lastProbeMs = 0; m_maxStallMs = 0; m_maxStallAtMs = 0; m_stallsOver20 = 0;
        m_stallLog.clear();
        m_stallProbe.setTimerType(Qt::PreciseTimer);
        m_stallProbe.setInterval(2);
        m_stallProbe.start();
    }
    // Deadzone boyunca kat edilen mesafe: pencere henüz hareket etmediği için
    // QML local koordinatları burada hâlâ geçerli.
    queueMove(initialDx, initialDy);
}

QPoint LayerShellWindow::dragBy(double deltaX, double deltaY) {
    // Relative pointer aktifken QML local delta'ları kendi hareketimizi
    // geri beslediği için yok sayılır (yarı hız + titreme kök nedeni).
    if (!m_isDragging || m_dragUsesRelativePointer) return QPoint(0, 0);
    queueMove(deltaX, deltaY);
    return QPoint(0, 0);
}

void LayerShellWindow::onRelativeMotion(double dx, double dy) {
    if (m_isDragging && m_dragUsesRelativePointer) {
        if (m_dragSwitched) {
            if (!m_loggedFirstMotionAfterSwitch) {
                m_loggedFirstMotionAfterSwitch = true;
                qInfo() << "RavenSpot drag: relative motion resumed after output switch";
            }
            m_postSwitchIdle.start();
        }
        if (m_perfDrag) ++m_dragMotions;
        queueMove(dx, dy);
    }
}

void LayerShellWindow::queueMove(double dx, double dy) {
    if (dx == 0.0 && dy == 0.0) return;
    const bool wasIdle = (m_pendingDx == 0.0 && m_pendingDy == 0.0);
    m_pendingDx += dx;
    m_pendingDy += dy;
    // 8kHz olayları kare başına tek margin güncellemesine indirger;
    // uygulama afterAnimating'de (GUI thread, sync'ten hemen önce) yapılır.
    if (wasIdle && m_window) {
        m_window->requestUpdate();
    }
}

QPoint LayerShellWindow::globalWindowPos() const {
    QScreen *screen = m_window ? m_window->screen() : nullptr;
    const QPoint origin = screen ? screen->geometry().topLeft() : QPoint(0, 0);
    return origin + QPoint(m_posX, m_posY);
}

QRect LayerShellWindow::desktopBounds() const {
    QRect bounds;
    for (QScreen *s : QGuiApplication::screens()) bounds = bounds.united(s->geometry());
    return bounds;
}

void LayerShellWindow::applyMargins() {
    if (m_layerWindow) {
        // Bu karenin commit'iyle birlikte uygulanır (double-buffered state).
        m_layerWindow->setMargins(QMargins(m_posX, m_posY, 0, 0));
    }
    QScreen *screen = m_window ? m_window->screen() : nullptr;
    if (!m_isWayland && m_window && screen) {
        m_window->setPosition(screen->geometry().x() + m_posX, screen->geometry().y() + m_posY);
    }
}

void LayerShellWindow::flushPendingMove() {
    if (m_pendingDx == 0.0 && m_pendingDy == 0.0) return;
    if (m_perfDrag && m_isDragging) {
        const qint64 now = m_dragClock.elapsed();
        if (m_dragFlushes > 0) {
            m_maxFlushGapMs = std::max(m_maxFlushGapMs, now - m_lastFlushMs);
            if (now - m_lastFlushMs > 20 && m_stallLog.size() < 300)
                m_stallLog += QStringLiteral(" frameGap%1ms@%2(m%3)").arg(now - m_lastFlushMs).arg(m_lastFlushMs).arg(m_dragMotions - m_motionsAtFlush);
        }
        m_lastFlushMs = now;
        m_motionsAtFlush = m_dragMotions;
        ++m_dragFlushes;
    }

    m_accumulatedDeltaX += m_pendingDx;
    m_accumulatedDeltaY += m_pendingDy;
    m_pendingDx = 0.0;
    m_pendingDy = 0.0;

    const int intDx = static_cast<int>(std::round(m_accumulatedDeltaX));
    const int intDy = static_cast<int>(std::round(m_accumulatedDeltaY));
    if (intDx == 0 && intDy == 0) return;
    m_accumulatedDeltaX -= intDx;
    m_accumulatedDeltaY -= intDy;

    QScreen *current = m_window ? m_window->screen() : nullptr;
    if (!current) return;

    const int winW = m_window->width();
    const int winH = visibleHeight();
    const QPoint centerOffset(winW / 2, winH / 2);

    // Sanal konum imleci birebir izler; yalnızca tüm masaüstü sınırına sıkıştırılır.
    const QRect desk = desktopBounds();
    m_dragVirtual += QPoint(intDx, intDy);
    m_dragVirtual.setX(std::clamp(m_dragVirtual.x(), desk.left(), std::max(desk.left(), desk.right() + 1 - winW)));
    m_dragVirtual.setY(std::clamp(m_dragVirtual.y(), desk.top(), std::max(desk.top(), desk.bottom() + 1 - winH)));

    // Kartın merkezi hangi ekrandaysa pencere o ekrana aittir. Merkez hiçbir
    // ekranda değilse (ör. farklı yükseklikteki monitörlerin boşluğu) mevcut
    // ekranda kal ve merkezi o ekranın içinde tut.
    QScreen *target = QGuiApplication::screenAt(m_dragVirtual + centerOffset);
    if (!target) {
        target = current;
        const QRect g = current->geometry();
        QPoint c = m_dragVirtual + centerOffset;
        c.setX(std::clamp(c.x(), g.left(), g.right()));
        c.setY(std::clamp(c.y(), g.top(), g.bottom()));
        m_dragVirtual = c - centerOffset;
    }

    // Hedef ekranda konum: dikeyde tamamen içeride; yatayda komşu ekrana taşabilir
    // (merkez hâlâ bu ekranda olduğu sürece), böylece geçiş sınırda takılmaz.
    const QRect tg = target->geometry();
    QPoint local = m_dragVirtual - tg.topLeft();
    local.setY(std::clamp(local.y(), 0, std::max(0, tg.height() - winH)));
    if (!desk.contains(QRect(m_dragVirtual, QSize(winW, winH)))) {
        local.setX(std::clamp(local.x(), 0, std::max(0, tg.width() - winW)));
    }

    m_posX = local.x();
    m_posY = local.y();
    m_hasCustomPosition = true;

    if (!m_isWayland) {
        // X11: global konumlandırma serbest; Qt ekranı kendisi günceller.
        m_window->setPosition(tg.topLeft() + local);
        return;
    }
    if (target != current) {
        m_switchTarget = target;
        if (!m_switchQueued) {
            m_switchQueued = true;
            // afterAnimating içinde kendi yüzeyimizi yok etmeyelim: olay döngüsüne ertele.
            QMetaObject::invokeMethod(this, &LayerShellWindow::performScreenSwitch, Qt::QueuedConnection);
        }
        return;
    }
    applyMargins();
}

void LayerShellWindow::performScreenSwitch() {
    m_switchQueued = false;
    QScreen *target = m_switchTarget;
    if (!target || !m_window || target == m_window->screen()) return;

    qInfo().noquote() << "RavenSpot drag: switching output" << m_window->screen()->name() << "->" << target->name()
                      << "at" << m_posX << m_posY << "dragging=" << m_isDragging;

    // Odak kaybı ve QML MouseArea iptali menüyü kapatmasın / sürüklemeyi bitirmesin.
    m_switchingScreen = true;
    m_switchGuard.start();
    m_graceTimer.stop();
    m_inRemap = true;

    m_window->setVisible(false);
    if (m_layerWindow) m_layerWindow->setScreen(target);
    m_window->setScreen(target);
    m_lastScreen = target;
    applyMargins();
    m_window->setVisible(true);
    m_window->requestActivate();

    m_inRemap = false;
    if (m_isDragging) {
        m_dragSwitched = true;
        m_loggedFirstMotionAfterSwitch = false;
        m_postSwitchIdle.start();
    }
    m_canCloseOnInactive = false;
    m_graceTimer.start();
    emit screenChanged(target);
}

void LayerShellWindow::updateDrag(double localX, double localY) {
    Q_UNUSED(localX);
    Q_UNUSED(localY);
}

void LayerShellWindow::endDrag() {
    // Yüzey yeniden yaratılırken QML MouseArea onCanceled -> endDrag gelir;
    // sürükleme gerçekte devam ediyor (bitişi ham düğme olayı belirler).
    if (m_inRemap) return;
    if (!m_isDragging) return;
    flushPendingMove();
    if (m_perfDrag) {
        const qint64 ms = std::max<qint64>(1, m_dragClock.elapsed());
        qInfo().nospace() << "RavenSpot drag perf: " << ms << " ms, motions=" << m_dragMotions
                          << " moves=" << m_dragFlushes << " (" << (m_dragFlushes * 1000 / ms) << "/s)"
                          << " swaps=" << m_dragSwaps.load() << " (" << (m_dragSwaps.load() * 1000 / ms) << "/s)"
                          << " maxGap=" << m_maxFlushGapMs << " ms relative=" << m_dragUsesRelativePointer
                          << " guiStallMax=" << m_maxStallMs << "ms@" << m_maxStallAtMs
                          << " stalls>20ms=" << m_stallsOver20 << " [" << m_stallLog << " ]";
        m_stallProbe.stop();
        m_perfDrag = false;
    }
    // Bırakınca kartı bulunduğu ekranın tamamen içine al (komşu ekrana taşmış kalmasın).
    if (QScreen *screen = m_window ? m_window->screen() : nullptr) {
        const int maxX = std::max(0, screen->geometry().width() - m_window->width());
        const int maxY = std::max(0, screen->geometry().height() - visibleHeight());
        const int x = std::clamp(m_posX, 0, maxX);
        const int y = std::clamp(m_posY, 0, maxY);
        if (x != m_posX || y != m_posY) {
            m_posX = x;
            m_posY = y;
            applyMargins();
            m_window->requestUpdate();
        }
    }
    m_isDragging = false;
    m_dragSwitched = false;
    m_postSwitchIdle.stop();
    m_dragUsesRelativePointer = false;
    m_accumulatedDeltaX = 0.0;
    m_accumulatedDeltaY = 0.0;
    m_pendingDx = 0.0;
    m_pendingDy = 0.0;
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
    if (m_switchingScreen) return;
    if (m_window && !m_window->isActive()) {
        hideWindow();
    }
}

void LayerShellWindow::onWindowActiveChanged() {
    if (!m_window) return;

    if (m_switchingScreen) {
        if (m_window->isActive()) m_switchingScreen = false; // yeni ekranda odak geri geldi
        return;
    }
    if (m_window->isActive()) {
        m_graceTimer.start();
    } else if (m_canCloseOnInactive && m_window->isVisible()) {
        hideWindow();
    }
}

void LayerShellWindow::onFocusWindowChanged(QWindow *focusWindow) {
    if (!m_window || !m_window->isVisible() || !m_canCloseOnInactive || m_switchingScreen) return;

    if (focusWindow != m_window.data()) {
        hideWindow();
    }
}

} // namespace ravenspot
