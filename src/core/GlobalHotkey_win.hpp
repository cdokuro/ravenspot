#pragma once
// System-wide hotkey for Windows (RegisterHotKey + WM_HOTKEY native filter).
// Linux uses the desktop's own shortcut (KDE) calling "ravenspot --toggle".

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

namespace ravenspot {

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    // spec like "alt+space", "ctrl+alt+space", "win+shift+r". Returns false if
    // the combination is invalid or already taken by another program.
    bool registerHotkey(const QString &spec);
    void unregisterHotkey();
    [[nodiscard]] QString current() const { return m_spec; }

    // Parses a spec into Win32 MOD_* flags and a virtual key. Exposed for tests.
    static bool parseSpec(const QString &spec, unsigned int *modifiers, unsigned int *vk);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();

private:
    int m_id{0};
    QString m_spec;
};

} // namespace ravenspot
