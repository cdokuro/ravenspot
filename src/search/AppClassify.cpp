#include "AppClassify.hpp"

#include <QStringList>

namespace ravenspot {

bool isNonUserApp(const QString &id, const QString &label, const QString &categories) {
    Q_UNUSED(categories)
    if (id.startsWith(QStringLiteral("kcm_"))) return true;

    const QString l = label.toLower();
    if (l.startsWith(QStringLiteral("uninstall ")) || l.contains(QStringLiteral("(system tray)"))) return true;

    // Araç zincirlerinin yanında gelen yardımcılar ve kopya ayar girişleri
    static const QStringList helperIds = {
        QStringLiteral("qv4l2"), QStringLiteral("qvidcap"), QStringLiteral("qdbusviewer"),
        QStringLiteral("assistant"), QStringLiteral("linguist"), QStringLiteral("lstopo"),
        QStringLiteral("kdesystemsettings"), QStringLiteral("org.kde.kdeconnect.nonplasma"),
    };
    if (helperIds.contains(id)) return true;
    if (id.startsWith(QStringLiteral("jconsole-")) || id.startsWith(QStringLiteral("jshell-"))) return true;
    return false;
}

} // namespace ravenspot
