#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

// Yapıştırılan/yazılan metnin komut olup olmadığını, tehlikesini ve nasıl
// çalıştırılacağını belirleyen saf fonksiyonlar. Komut bash'e her zaman TEK
// argv öğesi olarak verilir; RavenSpot hiçbir şeyi kendi shell'inde genişletmez.
namespace ravenspot::cmd {

enum class Signal { None = 0, Weak = 1, Strong = 2 };

using ExecutableCheck = std::function<bool(const QString &)>;

Signal detect(const QString &text, const ExecutableCheck &isExecutable);
bool isDangerous(const QString &command);
bool needsTerminal(const QString &command);
QStringList terminalArgs(const QString &command);
QStringList inlineArgs(const QString &command);
QString firstLine(const QString &command);
int extraLineCount(const QString &command);
QVariantMap analyze(const QString &text, const ExecutableCheck &isExecutable);

} // namespace ravenspot::cmd
