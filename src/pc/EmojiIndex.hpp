#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace ravenspot {

// ":" araması için emoji dizini (assets/emoji.json, gemoji MIT). İlk aramada yüklenir.
// Sıralama: tam takma ad > takma ad öneki > etiket/açıklama kelime öneki.
class EmojiIndex {
public:
    bool load(const QString &jsonPath);
    [[nodiscard]] bool isLoaded() const { return m_loaded; }
    [[nodiscard]] QVariantList search(const QString &query, int limit = 20) const; // [{glyph, name}]

private:
    struct Entry { QString glyph; QString name; QStringList aliases; QStringList words; };
    QList<Entry> m_entries;
    bool m_loaded{false};
};

} // namespace ravenspot
