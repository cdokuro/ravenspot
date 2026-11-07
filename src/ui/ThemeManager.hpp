#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>
#include <QMap>

namespace ravenspot {

struct ThemeTokens {
    QString key;
    QString name;
    QString desc;
    QString font{QStringLiteral("Inter")};
    QString iconTheme{QStringLiteral("candy-icons")};
    QString bgColor{QStringLiteral("#0b0c10")};
    QString bgBorder{QStringLiteral("#ffd700")};
    QString glowColor{QStringLiteral("#ffd700")};
    QString selBg{QStringLiteral("#1f51ff")};
    QString selFg{QStringLiteral("#ffffff")};
    QString fgColor{QStringLiteral("#f0f2f5")};
    QString fgDim{QStringLiteral("#8b949e")};
    QString accent{QStringLiteral("#ffd700")};
    QString cpuColor{QStringLiteral("#ffd700")};
    QString gpuColor{QStringLiteral("#00f0ff")};
    // Yapısal stil: "neon" (parlayan kenar, dolgu seçim) veya "edna" (düz
    // Material yüzeyler, gölge, sol vurgu çubuklu seçim, tema ikonları)
    QString style{QStringLiteral("neon")};
    QString surface{QStringLiteral("#12ffffff")};   // arama kutusu / düğme zemini
    QString panel{QStringLiteral("#18000000")};     // alt bilgi şeridi
    QString cardBorder;                             // boşsa bgBorder kullanılır

    [[nodiscard]] QVariantMap toVariantMap() const;
    static ThemeTokens fromJsonObject(const QString &key, const QJsonObject &obj, const ThemeTokens &defaults);
    static ThemeTokens defaultRavenCyber();
    static ThemeTokens ednaTheme();
    static QMap<QString, ThemeTokens> builtInThemes();
};

class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeThemeKey READ activeThemeKey NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap currentTheme READ currentTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes NOTIFY themesLoaded)

public:
    explicit ThemeManager(QObject *parent = nullptr);
    ~ThemeManager() override = default;

    // Loading & Persistence
    void loadThemes();
    bool saveActiveTheme(const QString &key);

    // QML-compatible API
    [[nodiscard]] Q_INVOKABLE QVariantMap currentTheme() const;
    [[nodiscard]] Q_INVOKABLE QString listRavenThemes() const;
    [[nodiscard]] Q_INVOKABLE QVariantList listThemesVariant() const;
    Q_INVOKABLE bool applyTheme(const QString &key);
    Q_INVOKABLE bool applyRavenTheme(const QString &key) { return applyTheme(key); }
    Q_INVOKABLE void reload();

    // C++ Getters
    [[nodiscard]] QString activeThemeKey() const { return m_activeKey; }
    [[nodiscard]] ThemeTokens currentTokens() const;
    [[nodiscard]] QStringList availableThemes() const { return m_themes.keys(); }
    [[nodiscard]] bool hasTheme(const QString &key) const { return m_themes.contains(key); }

    static QString sanitizeColor(const QString &hex, const QString &defaultColor);

signals:
    void themeChanged(const QString &themeKey);
    void themesLoaded();

private:
    void loadBuiltIns();
    bool parseThemesFile(const QString &filePath);
    void loadUserSelection();
    static QString resolveThemesJsonPath();
    static QString userConfigThemePath();

    QString m_activeKey{QStringLiteral("raven_cyber")};
    QMap<QString, ThemeTokens> m_themes;
};

} // namespace ravenspot
