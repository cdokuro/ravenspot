#include "pc/EmojiIndex.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>

namespace ravenspot {

bool EmojiIndex::load(const QString &jsonPath) {
    m_entries.clear();
    m_loaded = false;
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return false;
    static const QRegularExpression sep(QStringLiteral("[\\s_-]+"));
    const QJsonArray arr = doc.array();
    m_entries.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonArray e = v.toArray();
        if (e.size() < 4) continue;
        Entry en;
        en.glyph = e.at(0).toString();
        en.name = e.at(1).toString();
        en.aliases = e.at(2).toString().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        en.words = (en.name + QLatin1Char(' ') + e.at(3).toString()).toLower().split(sep, Qt::SkipEmptyParts);
        if (!en.glyph.isEmpty()) m_entries << en;
    }
    m_loaded = !m_entries.isEmpty();
    return m_loaded;
}

QVariantList EmojiIndex::search(const QString &query, int limit) const {
    QString q = query.trimmed().toLower();
    if (q.startsWith(QLatin1Char(':'))) q.remove(0, 1);

    struct Hit { int score; qsizetype order; };
    QList<Hit> hits;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        int score = -1;
        if (q.isEmpty()) score = 3;
        else if (e.aliases.contains(q)) score = 0;
        else if (std::any_of(e.aliases.begin(), e.aliases.end(), [&](const QString &a) { return a.startsWith(q); })) score = 1;
        else if (std::any_of(e.words.begin(), e.words.end(), [&](const QString &w) { return w.startsWith(q); })) score = 2;
        if (score >= 0) hits.push_back({score, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score < b.score; });

    QVariantList out;
    for (const Hit &h : hits) {
        if (out.size() >= limit) break;
        const Entry &e = m_entries.at(h.order);
        out << QVariantMap{{QStringLiteral("glyph"), e.glyph}, {QStringLiteral("name"), e.name}};
    }
    return out;
}

} // namespace ravenspot
