#include "ThemeManager.hpp"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QIcon>
#include <QRegularExpression>
#include <QDebug>

namespace ravenspot {

namespace {
const QRegularExpression s_hexColorRegex(QStringLiteral("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
}

QVariantMap ThemeTokens::toVariantMap() const {
    QVariantMap map;
    map[QStringLiteral("key")] = key;
    map[QStringLiteral("name")] = name;
    map[QStringLiteral("desc")] = desc;
    map[QStringLiteral("font")] = font;
    map[QStringLiteral("iconTheme")] = iconTheme;
    map[QStringLiteral("bgColor")] = bgColor;
    map[QStringLiteral("bgBorder")] = bgBorder;
    map[QStringLiteral("glowColor")] = glowColor;
    map[QStringLiteral("selBg")] = selBg;
    map[QStringLiteral("selFg")] = selFg;
    map[QStringLiteral("fgColor")] = fgColor;
    map[QStringLiteral("fgDim")] = fgDim;
    map[QStringLiteral("accent")] = accent;
    map[QStringLiteral("cpuColor")] = cpuColor;
    map[QStringLiteral("gpuColor")] = gpuColor;
    return map;
}

ThemeTokens ThemeTokens::defaultRavenCyber() {
    ThemeTokens t;
    t.key = QStringLiteral("raven_cyber");
    t.name = QStringLiteral("Raven Cyber");
    t.desc = QStringLiteral("Cyberpunk koyu obsidyen zemin ve altın sarısı neon parıltı");
    t.font = QStringLiteral("Inter");
    t.iconTheme = QStringLiteral("candy-icons");
    t.bgColor = QStringLiteral("#0b0c10");
    t.bgBorder = QStringLiteral("#ffd700");
    t.glowColor = QStringLiteral("#ffd700");
    t.selBg = QStringLiteral("#1f51ff");
    t.selFg = QStringLiteral("#ffffff");
    t.fgColor = QStringLiteral("#f0f2f5");
    t.fgDim = QStringLiteral("#8b949e");
    t.accent = QStringLiteral("#ffd700");
    t.cpuColor = QStringLiteral("#ffd700");
    t.gpuColor = QStringLiteral("#00f0ff");
    return t;
}

QMap<QString, ThemeTokens> ThemeTokens::builtInThemes() {
    QMap<QString, ThemeTokens> map;
    map[QStringLiteral("raven_cyber")] = defaultRavenCyber();

    ThemeTokens cachy;
    cachy.key = QStringLiteral("cachy_emerald");
    cachy.name = QStringLiteral("Cachy Emerald");
    cachy.desc = QStringLiteral("CachyOS zümrüt yeşili ve modern koyu arayüz");
    cachy.font = QStringLiteral("Inter");
    cachy.iconTheme = QStringLiteral("candy-icons");
    cachy.bgColor = QStringLiteral("#0a1312");
    cachy.bgBorder = QStringLiteral("#00f5a0");
    cachy.glowColor = QStringLiteral("#00f5a0");
    cachy.selBg = QStringLiteral("#007a50");
    cachy.selFg = QStringLiteral("#ffffff");
    cachy.fgColor = QStringLiteral("#e8faf2");
    cachy.fgDim = QStringLiteral("#6b8f84");
    cachy.accent = QStringLiteral("#00f5a0");
    cachy.cpuColor = QStringLiteral("#00f5a0");
    cachy.gpuColor = QStringLiteral("#00d2ff");
    map[cachy.key] = cachy;

    ThemeTokens mars;
    mars.key = QStringLiteral("sweet_mars");
    mars.name = QStringLiteral("Sweet Mars");
    mars.desc = QStringLiteral("KDE Sweet Mars neon mor/pembe synthwave estetiği");
    mars.font = QStringLiteral("Inter");
    mars.iconTheme = QStringLiteral("candy-icons");
    mars.bgColor = QStringLiteral("#110b1a");
    mars.bgBorder = QStringLiteral("#ff2a6d");
    mars.glowColor = QStringLiteral("#ff2a6d");
    mars.selBg = QStringLiteral("#7928ca");
    mars.selFg = QStringLiteral("#ffffff");
    mars.fgColor = QStringLiteral("#fce7f3");
    mars.fgDim = QStringLiteral("#9d8ba7");
    mars.accent = QStringLiteral("#ff2a6d");
    mars.cpuColor = QStringLiteral("#ff2a6d");
    mars.gpuColor = QStringLiteral("#05d9e8");
    map[mars.key] = mars;

    ThemeTokens frost;
    frost.key = QStringLiteral("nordic_frost");
    frost.name = QStringLiteral("Nordic Frost");
    frost.desc = QStringLiteral("Kuzey buzulu soğuk mavi ve sade İskandinav tonları");
    frost.font = QStringLiteral("Inter");
    frost.iconTheme = QStringLiteral("candy-icons");
    frost.bgColor = QStringLiteral("#0d1117");
    frost.bgBorder = QStringLiteral("#5e81ac");
    frost.glowColor = QStringLiteral("#88c0d0");
    frost.selBg = QStringLiteral("#434c5e");
    frost.selFg = QStringLiteral("#eceff4");
    frost.fgColor = QStringLiteral("#d8dee9");
    frost.fgDim = QStringLiteral("#7b889b");
    frost.accent = QStringLiteral("#88c0d0");
    frost.cpuColor = QStringLiteral("#88c0d0");
    frost.gpuColor = QStringLiteral("#81a1c1");
    map[frost.key] = frost;

    ThemeTokens obsidian;
    obsidian.key = QStringLiteral("obsidian_stealth");
    obsidian.name = QStringLiteral("Obsidian Stealth");
    obsidian.desc = QStringLiteral("Minimalist mat siyah monokrom ve saf beyaz ışıltı");
    obsidian.font = QStringLiteral("Inter");
    obsidian.iconTheme = QStringLiteral("candy-icons");
    obsidian.bgColor = QStringLiteral("#080808");
    obsidian.bgBorder = QStringLiteral("#e5e5e5");
    obsidian.glowColor = QStringLiteral("#ffffff");
    obsidian.selBg = QStringLiteral("#262626");
    obsidian.selFg = QStringLiteral("#ffffff");
    obsidian.fgColor = QStringLiteral("#f5f5f5");
    obsidian.fgDim = QStringLiteral("#737373");
    obsidian.accent = QStringLiteral("#ffffff");
    obsidian.cpuColor = QStringLiteral("#e5e5e5");
    obsidian.gpuColor = QStringLiteral("#a3a3a3");
    map[obsidian.key] = obsidian;

    return map;
}

QString ThemeManager::sanitizeColor(const QString &hex, const QString &defaultColor) {
    if (hex.trimmed().isEmpty() || !s_hexColorRegex.match(hex).hasMatch()) {
        return defaultColor;
    }
    return hex;
}

ThemeTokens ThemeTokens::fromJsonObject(const QString &key, const QJsonObject &obj, const ThemeTokens &defaults) {
    ThemeTokens t = defaults;
    t.key = key;

    if (obj.contains(QStringLiteral("name"))) t.name = obj[QStringLiteral("name")].toString();
    if (obj.contains(QStringLiteral("desc"))) t.desc = obj[QStringLiteral("desc")].toString();
    if (obj.contains(QStringLiteral("font"))) t.font = obj[QStringLiteral("font")].toString();
    if (obj.contains(QStringLiteral("iconTheme"))) t.iconTheme = obj[QStringLiteral("iconTheme")].toString();

    t.bgColor = ThemeManager::sanitizeColor(obj[QStringLiteral("bgColor")].toString(), defaults.bgColor);
    t.bgBorder = ThemeManager::sanitizeColor(obj[QStringLiteral("bgBorder")].toString(), defaults.bgBorder);
    t.glowColor = ThemeManager::sanitizeColor(obj[QStringLiteral("glowColor")].toString(), defaults.glowColor);
    t.selBg = ThemeManager::sanitizeColor(obj[QStringLiteral("selBg")].toString(), defaults.selBg);
    t.selFg = ThemeManager::sanitizeColor(obj[QStringLiteral("selFg")].toString(), defaults.selFg);
    t.fgColor = ThemeManager::sanitizeColor(obj[QStringLiteral("fgColor")].toString(), defaults.fgColor);
    t.fgDim = ThemeManager::sanitizeColor(obj[QStringLiteral("fgDim")].toString(), defaults.fgDim);
    t.accent = ThemeManager::sanitizeColor(obj[QStringLiteral("accent")].toString(), defaults.accent);
    t.cpuColor = ThemeManager::sanitizeColor(obj[QStringLiteral("cpuColor")].toString(), defaults.cpuColor);
    t.gpuColor = ThemeManager::sanitizeColor(obj[QStringLiteral("gpuColor")].toString(), defaults.gpuColor);

    return t;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    loadThemes();
}

void ThemeManager::loadBuiltIns() {
    m_themes = ThemeTokens::builtInThemes();
}

QString ThemeManager::resolveThemesJsonPath() {
    // 1. User customized themes
    const QString userPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot/themes.json");
    if (QFile::exists(userPath)) return userPath;

    // 2. Application directory
    const QString appDirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/themes.json");
    if (QFile::exists(appDirPath)) return appDirPath;

    // 3. Current working directory
    const QString cwdPath = QDir::currentPath() + QStringLiteral("/themes.json");
    if (QFile::exists(cwdPath)) return cwdPath;

    // 4. Reference ravenspot folder (development fallback)
    const QString refPath = QStringLiteral("/home/dokuro/Masaüstü/ravenspot/themes.json");
    if (QFile::exists(refPath)) return refPath;

    // 5. System installed location
    const QString sysPath = QStringLiteral("/usr/share/ravenspot/themes.json");
    if (QFile::exists(sysPath)) return sysPath;

    // 6. Embedded QRC
    if (QFile::exists(QStringLiteral(":/themes.json"))) return QStringLiteral(":/themes.json");

    return QString();
}

QString ThemeManager::userConfigThemePath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ravenspot/theme.json");
}

bool ThemeManager::parseThemesFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ThemeManager: JSON parse error in" << filePath << ":" << parseError.errorString();
        return false;
    }

    const QJsonObject rootObj = doc.object();
    if (!rootObj.contains(QStringLiteral("themes")) || !rootObj[QStringLiteral("themes")].isObject()) {
        return false;
    }

    const QJsonObject themesObj = rootObj[QStringLiteral("themes")].toObject();
    const ThemeTokens cyberDefaults = ThemeTokens::defaultRavenCyber();

    for (auto it = themesObj.begin(); it != themesObj.end(); ++it) {
        const QString themeKey = it.key();
        if (it.value().isObject()) {
            m_themes[themeKey] = ThemeTokens::fromJsonObject(themeKey, it.value().toObject(), cyberDefaults);
        }
    }

    if (rootObj.contains(QStringLiteral("active"))) {
        const QString act = rootObj[QStringLiteral("active")].toString();
        if (m_themes.contains(act)) {
            m_activeKey = act;
        }
    }

    return true;
}

void ThemeManager::loadUserSelection() {
    const QString path = userConfigThemePath();
    if (!QFile::exists(path)) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        const QString savedActive = doc.object()[QStringLiteral("active")].toString();
        if (m_themes.contains(savedActive)) {
            m_activeKey = savedActive;
        }
    }
}

void ThemeManager::loadThemes() {
    loadBuiltIns(); // Always start with safe built-in defaults

    const QString path = resolveThemesJsonPath();
    if (!path.isEmpty()) {
        parseThemesFile(path);
    }

    loadUserSelection();

    // Set initial icon theme in QIcon
    QIcon::setThemeName(currentTokens().iconTheme);

    emit themesLoaded();
    emit themeChanged(m_activeKey);
}

bool ThemeManager::saveActiveTheme(const QString &key) {
    const QString filePath = userConfigThemePath();
    const QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "ThemeManager: Could not open" << filePath << "for writing";
        return false;
    }

    QJsonObject obj;
    obj[QStringLiteral("active")] = key;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QVariantMap ThemeManager::currentTheme() const {
    return currentTokens().toVariantMap();
}

ThemeTokens ThemeManager::currentTokens() const {
    return m_themes.value(m_activeKey, ThemeTokens::defaultRavenCyber());
}

QString ThemeManager::listRavenThemes() const {
    QJsonArray array;
    for (auto it = m_themes.begin(); it != m_themes.end(); ++it) {
        const ThemeTokens &t = it.value();
        QJsonObject obj;
        obj[QStringLiteral("key")] = t.key;
        obj[QStringLiteral("name")] = t.name;
        obj[QStringLiteral("desc")] = t.desc;
        obj[QStringLiteral("isCurrent")] = (t.key == m_activeKey);
        obj[QStringLiteral("accent")] = t.accent;
        obj[QStringLiteral("font")] = t.font;
        obj[QStringLiteral("iconTheme")] = t.iconTheme;
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVariantList ThemeManager::listThemesVariant() const {
    QVariantList list;
    for (auto it = m_themes.begin(); it != m_themes.end(); ++it) {
        const ThemeTokens &t = it.value();
        QVariantMap map;
        map[QStringLiteral("key")] = t.key;
        map[QStringLiteral("name")] = t.name;
        map[QStringLiteral("desc")] = t.desc;
        map[QStringLiteral("isCurrent")] = (t.key == m_activeKey);
        map[QStringLiteral("accent")] = t.accent;
        map[QStringLiteral("font")] = t.font;
        map[QStringLiteral("iconTheme")] = t.iconTheme;
        list.append(map);
    }
    return list;
}

bool ThemeManager::applyTheme(const QString &key) {
    if (!m_themes.contains(key)) {
        qWarning() << "ThemeManager: Rejected switch to non-existent theme:" << key;
        return false;
    }

    m_activeKey = key;
    saveActiveTheme(key);

    const ThemeTokens tokens = currentTokens();
    QIcon::setThemeName(tokens.iconTheme);

    emit themeChanged(m_activeKey);
    return true;
}

void ThemeManager::reload() {
    loadThemes();
}

} // namespace ravenspot
