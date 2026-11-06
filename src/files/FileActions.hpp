#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Sağ tık menüsü için dosya/uygulama yardımcıları. Kabuk metni üretmez;
// dış programlar için yalnızca argv listeleri döner.
namespace ravenspot::files {

QString validateNewName(const QString &dir, const QString &newName);
QString renamePath(const QString &path, const QString &newName);
QVariantMap properties(const QString &path);
bool isArchive(const QString &path);
QStringList extractArgs(const QString &path);
QStringList compressArgs(const QString &path);
QStringList terminalInDirArgs(const QString &dir);
// "~", "~/x", olmayan dizin, dosya -> terminalin açılacağı mevcut dizin
QString resolveTerminalDir(const QString &path, const QString &home);

struct DesktopApp {
    QString file;
    QString name;
    QString icon;
    QStringList mimeTypes;
    bool noDisplay{false};
    bool hidden{false}; // Hidden=true: silinmiş sayılır (NoDisplay ise yalnızca menüde gizli)
};

DesktopApp parseDesktopFile(const QString &file);
QVariantList openWithApps(const QString &path, const QList<DesktopApp> &apps, const QString &defaultDesktopId);

QString autostartDir();
QString autostartPathFor(const QString &desktopFile);
bool isAutostart(const QString &desktopFile);
bool setAutostart(const QString &desktopFile, bool enabled);

QString uninstallCommand(const QString &desktopFile, const std::function<QString(const QString &)> &pacmanOwner);

} // namespace ravenspot::files
