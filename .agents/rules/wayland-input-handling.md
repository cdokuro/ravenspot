# Wayland LayerShell & High-Polling Input Guidelines

## 1. Wayland LayerSurface Double-Buffering & Commits
- When updating `LayerShellQt::Window::setMargins()`, the compositor (e.g., KWin) treats margin changes as pending state.
- Always call `m_window->requestUpdate()` after updating layer surface margins to trigger Qt Quick scene graph rendering, buffer swap, and the underlying `wl_surface.commit`.
- Never rely on mouse release or cursor shape changes to flush pending Wayland surface state.

## 2. Local vs. Global Coordinates in Wayland Dragging
- Wayland protocol does not provide unprivileged clients with global screen cursor coordinates (`QCursor::pos()` returns `(0, 0)`).
- Pointer events in Qt Quick `MouseArea` are strictly surface-local coordinates.
- **Never subtract the moved window offset from the last recorded mouse position (`lastX -= moved.x`)**. Because Wayland compositor surface commits are asynchronous, subtracting the requested move on sub-frame events induces an $\mathcal{O}(N^2)$ runaway feedback loop, teleporting the window across screens.
- Use pure local deltas (`dx = mouse.x - lastX; lastX = mouse.x`) with a deadzone threshold (e.g., 4px) to ignore accidental clicks.

## 3. High-Polling Rate (1000Hz - 8000Hz) Input Handling
- Gaming mice generate thousands of pointer events per second.
- Never trigger synchronous Wayland IPC requests or unthrottled JavaScript handlers per raw event.
- Accumulate sub-pixel/sub-frame deltas in C++ and align margin commits with the display V-Blank / scene graph frame swap (`QQuickWindow::frameSwapped`) to avoid dropped frames and phase-shifted stutter.
