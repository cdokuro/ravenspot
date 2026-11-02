#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <LayerShellQt/Window>

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

    // Drag-to-move window controls
    void beginDrag(double localX = 0, double localY = 0);
    QPoint dragBy(double deltaX, double deltaY);
    void updateDrag(double localX = 0, double localY = 0);
    void endDrag();
    void setWindowPosition(int x, int y);
    [[nodiscard]] QPoint windowPosition() const noexcept { return QPoint(m_posX, m_posY); }

signals:
    void visibilityChanged(bool visible);
    void screenChanged(QScreen *screen);

private slots:
    void onGraceTimerTriggered();
    void onWindowActiveChanged();
    void onFocusWindowChanged(QWindow *focusWindow);

private:
    void initWindow();
    void initLayerShell();
    void setupBlur();
    QScreen* resolveActiveScreen();

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
    QPoint m_dragStartWinPos;
    QPointF m_globalStartPos;
    QPointer<QScreen> m_lastScreen;
};

} // namespace ravenspot
