#pragma once

#include <QObject>
#include <QStringList>

namespace ravenspot {

// Son çalıştırılan komutlar: en yeni başta, tekrarsız, en fazla kMaxEntries.
// ~/.local/share/ravenspot/command_history.json (QSaveFile ile atomik yazım).
class CommandHistory : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 50;

    explicit CommandHistory(QString filePath = defaultPath(), QObject *parent = nullptr);

    static QString defaultPath();

    void add(const QString &command);
    [[nodiscard]] QStringList entries() const { return m_entries; }
    [[nodiscard]] QStringList matching(const QString &query, int limit = 5) const;

private:
    void load();
    void save() const;

    QString m_path;
    QStringList m_entries;
};

} // namespace ravenspot
