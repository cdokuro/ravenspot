#include "pc/RecentFiles.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <algorithm>

namespace ravenspot::pc {

QString defaultXbelPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel");
}

QVariantList recentFiles(const QString &xbelPath, const QString &query, int limit) {
    QFile f(xbelPath);
    if (!f.open(QIODevice::ReadOnly)) return {};

    struct Item { QString path; QDateTime modified; };
    QList<Item> items;
    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("bookmark")) continue;
        const auto a = xml.attributes();
        const QUrl url(a.value(QLatin1String("href")).toString());
        if (!url.isLocalFile()) continue; // yalnızca yerel dosyalar
        QString stamp = a.value(QLatin1String("modified")).toString();
        if (stamp.isEmpty()) stamp = a.value(QLatin1String("visited")).toString();
        items.push_back({url.toLocalFile(), QDateTime::fromString(stamp, Qt::ISODateWithMs)});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &x, const Item &y) { return x.modified > y.modified; });

    const QString q = query.trimmed();
    QVariantList out;
    QStringList seen;
    for (const Item &it : items) {
        if (out.size() >= limit) break;
        if (seen.contains(it.path)) continue;
        const QFileInfo fi(it.path);
        if (!fi.exists()) continue; // silinmiş dosyaları gösterme
        if (!q.isEmpty() && !fi.fileName().contains(q, Qt::CaseInsensitive)) continue;
        seen << it.path;
        out << QVariantMap{{QStringLiteral("path"), fi.absoluteFilePath()}, {QStringLiteral("name"), fi.fileName()},
                           {QStringLiteral("isDir"), fi.isDir()},
                           {QStringLiteral("modified"), it.modified.toLocalTime().toString(QStringLiteral("dd.MM.yyyy hh:mm"))}};
    }
    return out;
}

} // namespace ravenspot::pc
