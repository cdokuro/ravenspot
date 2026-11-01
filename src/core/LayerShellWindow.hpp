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
};

} // namespace ravenspot
