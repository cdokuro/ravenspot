#pragma once
// Pasted-link helpers: URL validation and the yt-dlp argv (no shell involved).
#include <QString>
#include <QStringList>

namespace ravenspot::video {

// true only for absolute http(s) URLs with a host
bool isHttpUrl(const QString &url);

// yt-dlp arguments: single video, progress one line per update, into downloadDir.
// The URL is placed after "--" so it can never be parsed as an option.
QStringList downloadArgs(const QString &url, const QString &downloadDir);

} // namespace ravenspot::video
