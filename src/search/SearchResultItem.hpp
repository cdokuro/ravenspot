#pragma once

#include <QString>
#include <QVariantMap>

namespace ravenspot {

struct SearchResultItem {
    QString id;          // Unique ID e.g. "app:org.kde.dolphin", "__calc__", "file:/path"
    QString name;        // Display label/title e.g. "Dolphin", "Shutdown", "= 26"
    QString comment;     // Subtitle / description / path
    QString icon;        // Nerd Font glyph e.g. "󰪚", "󰐥" or theme icon
    QString iconName;    // FreeDesktop/Candy icon name e.g. "system-shutdown", "accessories-calculator"
    QString type;        // "app", "krunner", "calc", "unit", "shell", "file", "bookmark", "web", "path"
    QString exec;        // Execution command, URL, or clipboard payload
    QString path;        // Category or breadcrumb tag e.g. "Sistem", "Hesapla"
    QString provider;    // Provider key e.g. "sys-action", "clipboard-item", "file-item", "open-url"
    QString value;       // Raw value e.g. "poweroff", "26", "6.21 miles"
    int score{0};        // Ranking score (lower = higher priority)
    bool disabled{false};
    QVariantMap extra;   // Additional custom metadata

    [[nodiscard]] QVariantMap toVariantMap() const {
        QVariantMap map;
        map[QStringLiteral("id")] = id;
        map[QStringLiteral("itemId")] = id;
        map[QStringLiteral("name")] = name;
        map[QStringLiteral("label")] = name;
        map[QStringLiteral("comment")] = comment;
        map[QStringLiteral("detail")] = comment;
        map[QStringLiteral("icon")] = icon;
        map[QStringLiteral("iconName")] = iconName;
        map[QStringLiteral("iconFont")] = QString();
        map[QStringLiteral("appIcon")] = QString();
        map[QStringLiteral("appId")] = QString();
        map[QStringLiteral("target")] = QString();
        map[QStringLiteral("path")] = path.isEmpty() ? comment : path;
        map[QStringLiteral("childCount")] = 0;
        map[QStringLiteral("action")] = exec;
        map[QStringLiteral("value")] = value.isEmpty() ? exec : value;
        map[QStringLiteral("desktop")] = (type == QLatin1String("app")) ? exec : QString();
        map[QStringLiteral("disabled")] = disabled;
        map[QStringLiteral("score")] = score;
        map[QStringLiteral("section")] = QString();
        map[QStringLiteral("kind")] = QStringLiteral("action");
        map[QStringLiteral("type")] = type;
        map[QStringLiteral("exec")] = exec;

        if (!provider.isEmpty()) {
            map[QStringLiteral("provider")] = provider;
        } else if (type == QLatin1String("file")) {
            map[QStringLiteral("provider")] = QStringLiteral("file-item");
            map[QStringLiteral("filePath")] = exec;
        } else if (type == QLatin1String("calc") || type == QLatin1String("unit")) {
            map[QStringLiteral("provider")] = QStringLiteral("clipboard-item");
        } else if (type == QLatin1String("krunner")) {
            map[QStringLiteral("provider")] = QStringLiteral("sys-action");
        } else if (type == QLatin1String("bookmark")) {
            map[QStringLiteral("provider")] = QStringLiteral("open-url");
        } else if (type == QLatin1String("path")) {
            map[QStringLiteral("provider")] = QStringLiteral("open-folder-path");
        }

        for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
            map.insert(it.key(), it.value());
        }

        return map;
    }
};

} // namespace ravenspot
