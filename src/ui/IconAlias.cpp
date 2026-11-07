#include "IconAlias.hpp"

#include <QHash>

namespace ravenspot {

QString iconAliasFor(const QString &themeName, const QString &iconName) {
    static const QHash<QString, QString> common = {
        {QStringLiteral("folder-download"), QStringLiteral("aptdaemon-download")},
        {QStringLiteral("folder-downloads"), QStringLiteral("aptdaemon-download")},
        {QStringLiteral("download"), QStringLiteral("aptdaemon-download")},
        {QStringLiteral("system-lock"), QStringLiteral("system-lock-screen")},
    };
    // kora'da bulunmayan adlar: miras alınan breeze yerine kora çizimi kullanılsın
    static const QHash<QString, QString> kora = {
        {QStringLiteral("cachy-update-blue"), QStringLiteral("system-software-update")},
        {QStringLiteral("preferences-desktop-keyboard-shortcut"), QStringLiteral("preferences-desktop-keyboard")},
        {QStringLiteral("input-keyboard-virtual"), QStringLiteral("input-keyboard")},
        {QStringLiteral("preferences-desktop-touchscreen"), QStringLiteral("input-touchscreen")},
        {QStringLiteral("hwloc"), QStringLiteral("cpu")},
        {QStringLiteral("btop"), QStringLiteral("utilities-system-monitor")},
        {QStringLiteral("java21-openjdk"), QStringLiteral("java")},
    };
    if (themeName == QStringLiteral("kora")) {
        const auto it = kora.constFind(iconName);
        if (it != kora.constEnd()) return it.value();
    }
    return common.value(iconName, iconName);
}

} // namespace ravenspot
