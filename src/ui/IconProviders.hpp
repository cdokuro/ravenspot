#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QHash>
#include <QString>
#include <list>

namespace ravenspot {

class IconCache {
public:
    explicit IconCache(size_t maxCapacity = 500);

    bool get(const QString &key, QImage &outImage);
    void put(const QString &key, const QImage &image);
    void clear();
    [[nodiscard]] size_t size() const;

private:
    size_t m_maxCapacity;
    mutable QMutex m_mutex;
    std::list<QString> m_lruList;
    QHash<QString, std::pair<QImage, std::list<QString>::iterator>> m_cache;
};

class AppIconProvider : public QQuickImageProvider {
public:
    explicit AppIconProvider();
    ~AppIconProvider() override = default;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void clearCache();

private:
    static QImage renderFallbackPlaceholder(const QSize &targetSize);
    static QString sanitizeIdentifier(const QString &rawId);

    IconCache m_cache{500};
};

class ThemeIconProvider : public QQuickImageProvider {
public:
    explicit ThemeIconProvider();
    ~ThemeIconProvider() override = default;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void clearCache();

private:
    static QImage renderFallbackPlaceholder(const QSize &targetSize);
    static QString sanitizeIdentifier(const QString &rawId);
    static QString resolveThemeAlias(const QString &cleanId);

    IconCache m_cache{500};
};

// Global helper to configure QIcon search paths & default theme
void setupIconThemeEnvironment();

} // namespace ravenspot
