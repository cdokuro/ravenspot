#include "run/CommandHistory.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace ravenspot {

CommandHistory::CommandHistory(QString filePath, QObject *parent)
    : QObject(parent)
    , m_path(std::move(filePath))
{
    load();
}

QString CommandHistory::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/ravenspot/command_history.json");
}

void CommandHistory::add(const QString &command) {
    const QString c = command.trimmed();
    if (c.isEmpty()) return;
    m_entries.removeAll(c);
    m_entries.prepend(c);
    while (m_entries.size() > kMaxEntries) m_entries.removeLast();
    save();
}

QStringList CommandHistory::matching(const QString &query, int limit) const {
    QStringList out;
    const QString q = query.trimmed();
    for (const QString &e : m_entries) {
        if (out.size() >= limit) break;
        if (q.isEmpty() || e.contains(q, Qt::CaseInsensitive)) out << e;
    }
    return out;
}

void CommandHistory::load() {
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return; // bozuk dosya: boş geçmişle devam et
    for (const auto &v : doc.array()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && !m_entries.contains(s)) m_entries << s;
        if (m_entries.size() >= kMaxEntries) break;
    }
}

void CommandHistory::save() const {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonArray::fromStringList(m_entries)).toJson(QJsonDocument::Indented));
    f.commit();
}

} // namespace ravenspot
