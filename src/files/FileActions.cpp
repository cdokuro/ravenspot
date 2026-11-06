#include "files/FileActions.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>

namespace ravenspot::files {

namespace {

QString formatSize(qint64 bytes) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return u == 0 ? QStringLiteral("%1 B").arg(bytes)
                  : QStringLiteral("%1 %2").arg(QString::number(v, 'f', 1), QLatin1String(units[u]));
}

QString permissionString(QFileDevice::Permissions p) {
    QString s;
    s += p.testFlag(QFileDevice::ReadOwner) ? 'r' : '-';
    s += p.testFlag(QFileDevice::WriteOwner) ? 'w' : '-';
    s += p.testFlag(QFileDevice::ExeOwner) ? 'x' : '-';
    s += p.testFlag(QFileDevice::ReadGroup) ? 'r' : '-';
    s += p.testFlag(QFileDevice::WriteGroup) ? 'w' : '-';
    s += p.testFlag(QFileDevice::ExeGroup) ? 'x' : '-';
    s += p.testFlag(QFileDevice::ReadOther) ? 'r' : '-';
    s += p.testFlag(QFileDevice::WriteOther) ? 'w' : '-';
    s += p.testFlag(QFileDevice::ExeOther) ? 'x' : '-';
    return s;
}

QString baseNameNoDesktop(const QString &desktopFile) {
    QString b = QFileInfo(desktopFile).fileName();
    if (b.endsWith(QLatin1String(".desktop"))) b.chop(8);
    return b;
}

} // namespace

QString validateNewName(const QString &dir, const QString &newName) {
    if (newName.trimmed().isEmpty()) return QStringLiteral("Name cannot be empty");
    if (newName.contains(QLatin1Char('/')) || newName.contains(QChar(0)))
        return QStringLiteral("Name cannot contain '/'");
    if (newName == QLatin1String(".") || newName == QLatin1String(".."))
        return QStringLiteral("'%1' is not a valid name").arg(newName);
    if (QFileInfo::exists(QDir(dir).filePath(newName)))
        return QStringLiteral("A file or folder with this name already exists");
    return QString();
}

QString renamePath(const QString &rawPath, const QString &newName) {
    const QFileInfo fi(QDir::cleanPath(rawPath)); // fd klasörleri sonda "/" ile verir
    if (!fi.exists() && !fi.isSymLink()) return QStringLiteral("File not found");
    if (newName == fi.fileName()) return QString();
    const QString dir = fi.absolutePath();
    const QString err = validateNewName(dir, newName);
    if (!err.isEmpty()) return err;
    if (!QDir().rename(fi.absoluteFilePath(), QDir(dir).filePath(newName)))
        return QStringLiteral("Rename failed (permission denied?)");
    return QString();
}

QVariantMap properties(const QString &rawPath) {
    const QFileInfo fi(QDir::cleanPath(rawPath));
    QMimeDatabase db;
    const QMimeType mt = db.mimeTypeForFile(fi);
    QVariantMap m;
    m.insert(QStringLiteral("name"), fi.fileName());
    m.insert(QStringLiteral("path"), fi.absoluteFilePath());
    m.insert(QStringLiteral("isDir"), fi.isDir());
    m.insert(QStringLiteral("isSymlink"), fi.isSymLink());
    m.insert(QStringLiteral("target"), fi.isSymLink() ? fi.symLinkTarget() : QString());
    m.insert(QStringLiteral("mime"), mt.name());
    m.insert(QStringLiteral("mimeComment"), mt.comment());
    if (fi.isDir()) {
        const int items = static_cast<int>(QDir(fi.absoluteFilePath())
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).size());
        m.insert(QStringLiteral("size"), qint64(-1));
        m.insert(QStringLiteral("items"), items);
        m.insert(QStringLiteral("sizeText"), QStringLiteral("%1 item%2").arg(items).arg(items == 1 ? "" : "s"));
    } else {
        m.insert(QStringLiteral("size"), fi.size());
        m.insert(QStringLiteral("items"), 0);
        m.insert(QStringLiteral("sizeText"), formatSize(fi.size()));
    }
    m.insert(QStringLiteral("modified"), fi.lastModified().toString(QStringLiteral("dd.MM.yyyy hh:mm")));
    m.insert(QStringLiteral("permissions"), permissionString(fi.permissions()));
    m.insert(QStringLiteral("owner"), fi.owner());
    return m;
}

bool isArchive(const QString &path) {
    static const QStringList archives = {
        QStringLiteral("application/zip"), QStringLiteral("application/x-tar"), QStringLiteral("application/gzip"),
        QStringLiteral("application/x-7z-compressed"), QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-rar"), QStringLiteral("application/x-xz"), QStringLiteral("application/zstd"),
        QStringLiteral("application/x-bzip2"), QStringLiteral("application/x-lzma"),
        QStringLiteral("application/x-compressed-tar"), QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-zstd-compressed-tar"), QStringLiteral("application/x-bzip2-compressed-tar")};
    const QFileInfo fi(path);
    if (!fi.isFile()) return false;
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(fi);
    for (const QString &a : archives) {
        if (mt.name() == a || mt.inherits(a)) return true;
    }
    return false;
}

QStringList extractArgs(const QString &path) {
    return {QStringLiteral("--batch"), QStringLiteral("--autodestination"), path};
}

QStringList compressArgs(const QString &path) {
    return {QStringLiteral("--batch"), QStringLiteral("--add"), QStringLiteral("--autofilename"),
            QStringLiteral("zip"), path};
}

QStringList terminalInDirArgs(const QString &dir) {
    // Yol tek argv öğesi ($1): boşluk, tırnak ve $(...) olduğu gibi kalır.
    return {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
            QStringLiteral("cd -- \"$1\" && exec \"${SHELL:-bash}\" -i"), QStringLiteral("ravenspot"), dir};
}

QString resolveTerminalDir(const QString &path, const QString &home) {
    QString p = path.trimmed();
    if (p.isEmpty() || p == QLatin1String("~")) return QDir::cleanPath(home);
    if (p.startsWith(QLatin1String("~/"))) p = home + p.mid(1);
    const QFileInfo fi(QDir::cleanPath(p));
    if (fi.isDir()) return fi.absoluteFilePath();
    if (fi.exists()) return fi.absolutePath(); // dosya: içinde bulunduğu klasör
    return QDir::cleanPath(home);               // olmayan yol: ev dizini (sessizce başarısız olma)
}

DesktopApp parseDesktopFile(const QString &file) {
    DesktopApp app;
    app.file = file;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return app;
    QTextStream in(&f);
    bool inEntry = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) { inEntry = (line == QLatin1String("[Desktop Entry]")); continue; }
        if (!inEntry || line.startsWith(QLatin1Char('#'))) continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Name")) app.name = val;
        else if (key == QLatin1String("Icon")) app.icon = val;
        else if (key == QLatin1String("MimeType")) app.mimeTypes = val.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("NoDisplay") && val == QLatin1String("true")) app.noDisplay = true;
        else if (key == QLatin1String("Hidden") && val == QLatin1String("true")) app.hidden = true;
    }
    return app;
}

QVariantList openWithApps(const QString &path, const QList<DesktopApp> &apps, const QString &defaultDesktopId) {
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(path);
    QStringList names = {mt.name()};
    names << mt.aliases() << mt.allAncestors();

    QList<DesktopApp> matched;
    for (const DesktopApp &a : apps) {
        // NoDisplay işleyiciler (ör. font yükleyici) dosya türü için geçerlidir; yalnızca Hidden silinmiş sayılır
        if (a.hidden || a.name.isEmpty()) continue;
        for (const QString &m : a.mimeTypes) {
            if (names.contains(m)) { matched << a; break; }
        }
    }
    auto isDefault = [&](const DesktopApp &a) {
        return !defaultDesktopId.isEmpty() && QFileInfo(a.file).fileName() == defaultDesktopId;
    };
    std::stable_sort(matched.begin(), matched.end(), [&](const DesktopApp &x, const DesktopApp &y) {
        if (isDefault(x) != isDefault(y)) return isDefault(x);
        return x.name.compare(y.name, Qt::CaseInsensitive) < 0;
    });
    QVariantList out;
    QStringList seen;
    for (const DesktopApp &a : matched) {
        const QString id = QFileInfo(a.file).fileName();
        if (seen.contains(id)) continue; // aynı uygulama birden çok dizinde
        seen << id;
        out << QVariantMap{{QStringLiteral("file"), a.file}, {QStringLiteral("name"), a.name},
                           {QStringLiteral("icon"), a.icon}, {QStringLiteral("isDefault"), isDefault(a)}};
    }
    return out;
}

QString autostartDir() {
    const QByteArray env = qgetenv("RAVENSPOT_AUTOSTART_DIR");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart");
}

QString autostartPathFor(const QString &desktopFile) {
    return autostartDir() + QLatin1Char('/') + QFileInfo(desktopFile).fileName();
}

bool isAutostart(const QString &desktopFile) {
    return !desktopFile.isEmpty() && QFileInfo::exists(autostartPathFor(desktopFile));
}

bool setAutostart(const QString &desktopFile, bool enabled) {
    const QString target = autostartPathFor(desktopFile);
    if (!enabled) return !QFileInfo::exists(target) || QFile::remove(target);
    if (!QFileInfo::exists(desktopFile)) return false;
    QDir().mkpath(autostartDir());
    if (QFileInfo::exists(target)) QFile::remove(target);
    return QFile::copy(desktopFile, target);
}

QString uninstallCommand(const QString &desktopFile, const std::function<QString(const QString &)> &pacmanOwner) {
    // Terminalde bash betiği olarak çalışacağı için adlar katı biçimde doğrulanır.
    static const QRegularExpression flatpakId(QStringLiteral("^[A-Za-z0-9._-]+$"));
    static const QRegularExpression pkgName(QStringLiteral("^[A-Za-z0-9@._+-]+$"));
    if (desktopFile.contains(QLatin1String("/flatpak/exports/share/applications/"))) {
        const QString id = baseNameNoDesktop(desktopFile);
        return flatpakId.match(id).hasMatch() ? QStringLiteral("flatpak uninstall ") + id : QString();
    }
    const QString owner = pacmanOwner ? pacmanOwner(desktopFile).trimmed() : QString();
    if (owner.isEmpty() || !pkgName.match(owner).hasMatch()) return QString();
    return QStringLiteral("sudo pacman -Rns ") + owner;
}

} // namespace ravenspot::files
