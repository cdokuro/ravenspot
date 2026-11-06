#include "IconProviders.hpp"

#include <QMutex>
#include <QMutexLocker>

#include <QIcon>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QCoreApplication>
#include <QStandardPaths>
#include <algorithm>

namespace ravenspot {

namespace {
QMutex g_lastIconMutex;
QString g_lastIcon;
void noteRequestedIcon(const QString &provider, const QString &id) {
    QMutexLocker lock(&g_lastIconMutex);
    g_lastIcon = provider + QLatin1Char(':') + id;
}
} // namespace

QString lastRequestedIcon() {
    QMutexLocker lock(&g_lastIconMutex);
    return g_lastIcon;
}

// =============================================================================
// LRU IconCache Implementation (Thread-Safe, Cap: 500 items)
// =============================================================================
IconCache::IconCache(size_t maxCapacity)
    : m_maxCapacity(maxCapacity)
{
}

bool IconCache::get(const QString &key, QImage &outImage) {
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return false;
    }
    m_lruList.erase(it->second);
    m_lruList.push_front(key);
    it->second = m_lruList.begin();
    outImage = it->first;
    return true;
}

void IconCache::put(const QString &key, const QImage &image) {
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_lruList.erase(it->second);
        m_lruList.push_front(key);
        it->second = m_lruList.begin();
        it->first = image;
        return;
    }

    if (m_cache.size() >= static_cast<qsizetype>(m_maxCapacity) && !m_lruList.empty()) {
        const QString oldest = m_lruList.back();
        m_lruList.pop_back();
        m_cache.remove(oldest);
    }

    m_lruList.push_front(key);
    m_cache.insert(key, {image, m_lruList.begin()});
}

void IconCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_lruList.clear();
}

size_t IconCache::size() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<size_t>(m_cache.size());
}

// =============================================================================
// Sanitization & Fallback Utilities
// =============================================================================
namespace {

QImage renderSyntheticPlaceholder(const QSize &size) {
    const int w = std::max(size.width(), 64);
    const int h = std::max(size.height(), 64);
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(128, 128, 128, 160));
    p.drawRoundedRect(4, 4, w - 8, h - 8, 8, 8);

    // Subtle inner symbol
    p.setPen(QColor(240, 240, 240, 220));
    QFont font = p.font();
    font.setPixelSize(w / 3);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QStringLiteral("?"));

    return img;
}

QString sanitizeId(const QString &rawId) {
    // 1. Strip QML version query parameter "?v=..."
    QString clean = rawId.contains(u'?') ? rawId.section(u'?', 0, 0) : rawId;
    clean = clean.trimmed();

    // 2. Prevent path traversal ("../")
    if (clean.contains(QStringLiteral(".."))) {
        clean = QFileInfo(clean).fileName();
    }

    // 3. Fallback on empty string
    if (clean.isEmpty()) {
        clean = QStringLiteral("applications-all");
    }

    return clean;
}

} // anonymous namespace

// =============================================================================
// AppIconProvider Implementation
// =============================================================================
AppIconProvider::AppIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void AppIconProvider::clearCache() {
    m_cache.clear();
}

QImage AppIconProvider::renderFallbackPlaceholder(const QSize &targetSize) {
    return renderSyntheticPlaceholder(targetSize);
}

QString AppIconProvider::sanitizeIdentifier(const QString &rawId) {
    return sanitizeId(rawId);
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString cleanId = sanitizeIdentifier(id);
    noteRequestedIcon(QStringLiteral("appicon"), cleanId);
    const int w = std::max(requestedSize.width() > 0 ? requestedSize.width() : 64, 64);
    const int h = std::max(requestedSize.height() > 0 ? requestedSize.height() : 64, 64);
    const QSize targetSize(w, h);
    const QString cacheKey = cleanId + QStringLiteral("@") + QString::number(w) + QStringLiteral("x") + QString::number(h);

    QImage cached;
    if (m_cache.get(cacheKey, cached)) {
        if (size) *size = cached.size();
        return cached;
    }

    QIcon icon;
    // Direct file path probe (with 0-byte corrupt file rejection)
    if (QFileInfo::exists(cleanId)) {
        const QFileInfo fi(cleanId);
        if (fi.size() > 16) {
            icon = QIcon(cleanId);
        }
    }

    // Theme lookup
    if (icon.isNull()) {
        icon = QIcon::fromTheme(cleanId);
    }

    // Fallbacks
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("applications-all"));
    }

    QImage result;
    if (!icon.isNull()) {
        const QPixmap pm = icon.pixmap(targetSize);
        if (!pm.isNull()) {
            result = pm.toImage();
        }
    }

    if (result.isNull()) {
        result = renderFallbackPlaceholder(targetSize);
    }

    m_cache.put(cacheKey, result);
    if (size) *size = result.size();
    return result;
}

// =============================================================================
// ThemeIconProvider Implementation
// =============================================================================
ThemeIconProvider::ThemeIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void ThemeIconProvider::clearCache() {
    m_cache.clear();
}

QImage ThemeIconProvider::renderFallbackPlaceholder(const QSize &targetSize) {
    return renderSyntheticPlaceholder(targetSize);
}

QString ThemeIconProvider::sanitizeIdentifier(const QString &rawId) {
    return sanitizeId(rawId);
}

QString ThemeIconProvider::resolveThemeAlias(const QString &cleanId) {
    if (cleanId == QStringLiteral("folder-download") ||
        cleanId == QStringLiteral("folder-downloads") ||
        cleanId == QStringLiteral("download")) {
        return QStringLiteral("aptdaemon-download");
    }
    if (cleanId == QStringLiteral("system-lock")) {
        return QStringLiteral("system-lock-screen");
    }
    return cleanId;
}

QImage ThemeIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString cleanId = resolveThemeAlias(sanitizeIdentifier(id));
    noteRequestedIcon(QStringLiteral("themeicon"), cleanId);
    const int w = std::max(requestedSize.width() > 0 ? requestedSize.width() : 64, 64);
    const int h = std::max(requestedSize.height() > 0 ? requestedSize.height() : 64, 64);
    const QSize targetSize(w, h);
    const QString cacheKey = cleanId + QStringLiteral("@") + QString::number(w) + QStringLiteral("x") + QString::number(h);

    QImage cached;
    if (m_cache.get(cacheKey, cached)) {
        if (size) *size = cached.size();
        return cached;
    }

    QIcon icon;
    if (QFileInfo::exists(cleanId)) {
        const QFileInfo fi(cleanId);
        if (fi.size() > 16) {
            icon = QIcon(cleanId);
        }
    }

    if (icon.isNull()) {
        icon = QIcon::fromTheme(cleanId);
    }

    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("applications-all"));
    }

    QImage result;
    if (!icon.isNull()) {
        const QPixmap pm = icon.pixmap(targetSize);
        if (!pm.isNull()) {
            result = pm.toImage();
        }
    }

    if (result.isNull()) {
        result = renderFallbackPlaceholder(targetSize);
    }

    m_cache.put(cacheKey, result);
    if (size) *size = result.size();
    return result;
}

// =============================================================================
// Global Environment Configuration
// =============================================================================
void setupIconThemeEnvironment() {
    QStringList paths = QIcon::themeSearchPaths();

    // 1. Project-local icons folder
    const QString localIcons = QCoreApplication::applicationDirPath() + QStringLiteral("/icons");
    if (QDir(localIcons).exists() && !paths.contains(localIcons)) {
        paths.prepend(localIcons);
    }

    const QString cwdIcons = QDir::currentPath() + QStringLiteral("/icons");
    if (QDir(cwdIcons).exists() && !paths.contains(cwdIcons)) {
        paths.prepend(cwdIcons);
    }

    // 2. Reference ravenspot icons folder
    const QString refIcons = QStringLiteral("/home/dokuro/Masaüstü/ravenspot/icons");
    if (QDir(refIcons).exists() && !paths.contains(refIcons)) {
        paths.append(refIcons);
    }

    // 3. User & System FreeDesktop paths
    const QString userIcons = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons");
    if (!paths.contains(userIcons)) {
        paths.append(userIcons);
    }

    QIcon::setThemeSearchPaths(paths);
    QIcon::setThemeName(QStringLiteral("candy-icons"));
}

} // namespace ravenspot
