#include "net/VideoDownload.hpp"

#include <QUrl>

namespace ravenspot::video {

bool isHttpUrl(const QString &url) {
    const QString t = url.trimmed();
    if (t.isEmpty() || t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char(' '))) return false;
    const QUrl u(t, QUrl::StrictMode);
    return u.isValid() && !u.host().isEmpty()
        && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"));
}

QStringList downloadArgs(const QString &url, const QString &downloadDir) {
    return {QStringLiteral("--no-playlist"), QStringLiteral("--newline"),
            QStringLiteral("-P"), downloadDir,
            QStringLiteral("-o"), QStringLiteral("%(title).150B [%(id)s].%(ext)s"),
            QStringLiteral("--"), url.trimmed()};
}

} // namespace ravenspot::video
