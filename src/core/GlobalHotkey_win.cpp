#include "core/GlobalHotkey_win.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <windows.h>

namespace ravenspot {

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey() {
    unregisterHotkey();
    if (auto *app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

bool GlobalHotkey::parseSpec(const QString &spec, unsigned int *modifiers, unsigned int *vk) {
    unsigned int mods = 0;
    unsigned int key = 0;
    const QStringList parts = spec.toLower().remove(QLatin1Char(' ')).split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    for (int i = 0; i < parts.size(); ++i) {
        const QString &p = parts.at(i);
        const bool last = (i == parts.size() - 1);
        if (p == QLatin1String("alt")) mods |= MOD_ALT;
        else if (p == QLatin1String("ctrl") || p == QLatin1String("control")) mods |= MOD_CONTROL;
        else if (p == QLatin1String("shift")) mods |= MOD_SHIFT;
        else if (p == QLatin1String("win") || p == QLatin1String("meta") || p == QLatin1String("super")) mods |= MOD_WIN;
        else if (!last) return false;
        else if (p == QLatin1String("space")) key = VK_SPACE;
        else if (p == QLatin1String("enter") || p == QLatin1String("return")) key = VK_RETURN;
        else if (p == QLatin1String("tab")) key = VK_TAB;
        else if (p.size() == 1 && p.at(0).isLetterOrNumber() && p.at(0).unicode() < 128) key = p.at(0).toUpper().unicode();
        else if (p.size() >= 2 && p.size() <= 3 && p.at(0) == QLatin1Char('f')) {
            bool ok = false;
            const int n = p.mid(1).toInt(&ok);
            if (!ok || n < 1 || n > 24) return false;
            key = VK_F1 + static_cast<unsigned int>(n - 1);
        } else return false;
    }
    if (key == 0 || mods == 0) return false; // a bare key would steal normal typing
    if (modifiers) *modifiers = mods;
    if (vk) *vk = key;
    return true;
}

bool GlobalHotkey::registerHotkey(const QString &spec) {
    unregisterHotkey();
    unsigned int mods = 0, vk = 0;
    if (!parseSpec(spec, &mods, &vk)) {
        qWarning() << "RavenSpot hotkey: invalid spec" << spec;
        return false;
    }
    m_id = 0x5253; // 'RS'
    if (!::RegisterHotKey(nullptr, m_id, mods | MOD_NOREPEAT, vk)) {
        qWarning() << "RavenSpot hotkey:" << spec << "is already in use (error" << ::GetLastError() << ")";
        m_id = 0;
        return false;
    }
    m_spec = spec;
    qInfo() << "RavenSpot hotkey registered:" << spec;
    return true;
}

void GlobalHotkey::unregisterHotkey() {
    if (m_id) {
        ::UnregisterHotKey(nullptr, m_id);
        m_id = 0;
        m_spec.clear();
    }
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(result)
    if (eventType != "windows_generic_MSG" || !m_id) return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == m_id) {
        emit activated();
        return true;
    }
    return false;
}

} // namespace ravenspot
