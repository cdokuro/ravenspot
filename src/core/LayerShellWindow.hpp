#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <LayerShellQt/Window>

#include "core/RelativePointer.hpp"

namespace ravenspot {

class LayerShellWindow : public QObject {
    Q_OBJECT

public:
    explicit LayerShellWindow(QQuickWindow *window = nullptr, QObject *parent = nullptr);
    ~LayerShellWindow() override;

    // Window visibility controls
    void attachWindow(QQuickWindow *window);
    void showOnActiveScreen();
    void showWindow() { showOnActiveScreen(); }
    void hideWindow();
    void toggleWindow();

    // Query state
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] QQuickWindow* window() const { return m_window.data(); }
    [[nodiscard]] QScreen* activeScreen() const;

    // Blur and geometry configuration
    void updateBlurRegion(int cardHeight = 600);
    void setCardHeight(int height);
    [[nodiscard]] int cardHeight() const noexcept { return m_cardHeight; }
    [[nodiscard]] int visibleHeight() const;

    // Drag-to-move window controls
    void beginDrag(double initialDx = 0, double initialDy = 0);
    QPoint dragBy(double deltaX, double deltaY);
    void updateDrag(double localX = 0, double localY = 0);
    void endDrag();
    void setWindowPosition(int x, int y);
    [[nodiscard]] QPoint windowPosition() const noexcept { return QPoint(m_posX, m_posY); }
    // Sürükleme sırasında yüzey başka ekranda yeniden yaratılırken true;
    // bu sürede odak kaybı menüyü kapatmamalı.
    [[nodiscard]] bool isSwitchingScreen() const noexcept { return m_switchingScreen; }

signals:
    void visibilityChanged(bool visible);
    void screenChanged(QScreen *screen);

private slots:
    void onGraceTimerTriggered();
    void onWindowActiveChanged();
    void onFocusWindowChanged(QWindow *focusWindow);
    void onRelativeMotion(double dx, double dy);
    void flushPendingMove();
    void performScreenSwitch();

private:
    void initWindow();
    void initLayerShell();
    void setupBlur();
    QScreen* resolveActiveScreen();
    void queueMove(double dx, double dy);
    [[nodiscard]] QPoint globalWindowPos() const;
    [[nodiscard]] QRect desktopBounds() const;
    void applyMargins();

    QPointer<QQuickWindow> m_window;
    bool m_ownsWindow{false};
    LayerShellQt::Window *m_layerWindow{nullptr};
    QTimer m_graceTimer;
    bool m_canCloseOnInactive{false};
    bool m_isWayland{false};
    int m_cardHeight{600};
    int m_posX{0};
    int m_posY{0};
    bool m_hasCustomPosition{false};
    bool m_isDragging{false};
    double m_accumulatedDeltaX{0.0};
    double m_accumulatedDeltaY{0.0};
    bool m_dragUsesRelativePointer{false};
    double m_pendingDx{0.0};
    double m_pendingDy{0.0};
    RelativePointer *m_relativePointer{nullptr};

    // Ekranlar arası sürükleme: imleci birebir izleyen, ekran sınırına
    // sıkıştırılmayan sanal global konum. Kartın merkezi başka ekrana geçince
    // layer yüzeyi oraya taşınır (layer-shell'de çıkış sonradan değişemez).
    QPoint m_dragVirtual;
    QPointer<QScreen> m_switchTarget;
    bool m_switchQueued{false};
    bool m_switchingScreen{false};
    bool m_inRemap{false};
    QTimer m_switchGuard;
    // Güvenlik: geçişten sonra bırakma olayı kaçarsa kart düğmesiz imleci izlemesin.
    bool m_dragSwitched{false};
    bool m_loggedFirstMotionAfterSwitch{false};
    QTimer m_postSwitchIdle;
    QPointer<QScreen> m_lastScreen;
};

} // namespace ravenspot
