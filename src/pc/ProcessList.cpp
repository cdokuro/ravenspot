#include "pc/ProcessList.hpp"

#include <QDir>
#include <QFile>
#include <QVariantMap>
#include <algorithm>

namespace ravenspot::pc {

namespace {

QByteArray readSmall(const QString &path, qint64 max = 64 * 1024) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.read(max);
}

QString rssText(int kb) {
    if (kb >= 1024 * 1024) return QStringLiteral("%1 GB").arg(QString::number(kb / 1048576.0, 'f', 1));
    if (kb >= 1024) return QStringLiteral("%1 MB").arg(QString::number(kb / 1024.0, 'f', 1));
    return QStringLiteral("%1 KB").arg(kb);
}

} // namespace

QVariantList listProcesses(const QString &query, const QString &procRoot, uint uid, int limit) {
    const QString q = query.trimmed();
    if (q.isEmpty()) return {};

    struct Row { int pid; QString name; QString cmd; int rssKb; };
    QList<Row> rows;
    const QStringList entries = QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries) {
        bool ok = false;
        const int pid = e.toInt(&ok);
        if (!ok || pid <= 1) continue; // PID 1 asla
        const QString dir = procRoot + QLatin1Char('/') + e + QLatin1Char('/');

        const QByteArray status = readSmall(dir + QStringLiteral("status"));
        if (status.isEmpty()) continue; // süreç bu arada bitti
        int procUid = -1;
        int rss = 0;
        for (const QByteArray &line : status.split('\n')) {
            if (line.startsWith("Uid:")) {
                const QList<QByteArray> f = line.mid(4).simplified().split(' ');
                if (!f.isEmpty()) procUid = f.first().toInt(); // gerçek UID
            } else if (line.startsWith("VmRSS:")) {
                rss = line.mid(6).simplified().split(' ').value(0).toInt();
            }
        }
        if (procUid < 0 || static_cast<uint>(procUid) != uid) continue; // yalnızca kendi süreçlerimiz

        const QString name = QString::fromUtf8(readSmall(dir + QStringLiteral("comm"), 256)).trimmed();
        QByteArray rawCmd = readSmall(dir + QStringLiteral("cmdline"), 4096);
        rawCmd.replace('\0', ' ');
        const QString cmd = QString::fromUtf8(rawCmd).trimmed();
        if (!name.contains(q, Qt::CaseInsensitive) && !cmd.contains(q, Qt::CaseInsensitive)) continue;
        rows.push_back({pid, name, cmd, rss});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.rssKb > b.rssKb; });

    QVariantList out;
    for (const Row &r : rows) {
        if (out.size() >= limit) break;
        out << QVariantMap{{QStringLiteral("pid"), r.pid}, {QStringLiteral("name"), r.name},
                           {QStringLiteral("cmd"), r.cmd}, {QStringLiteral("rssKb"), r.rssKb},
                           {QStringLiteral("rssText"), rssText(r.rssKb)}};
    }
    return out;
}

} // namespace ravenspot::pc
