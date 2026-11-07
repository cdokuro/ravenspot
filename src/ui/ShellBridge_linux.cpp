// Linux (KDE Plasma 6 / Wayland) ShellBridge methods: D-Bus, /proc, sysfs,
// Konsole/Dolphin. Moved verbatim from ShellBridge.cpp when the Windows port
// was added; Windows equivalents live in ShellBridge_win.cpp.
#include "ShellBridge.hpp"
#include "ThemeManager.hpp"
#include "../audio/AudioEngine.hpp"
#include "../core/LayerShellWindow.hpp"
#include "../search/AppIndexer.hpp"
#include "../run/CommandAnalyzer.hpp"
#include "../run/CommandRunner.hpp"
#include "../run/CommandHistory.hpp"
#include "pc/ClipboardHistory.hpp"
#include "pc/ProcessList.hpp"
#include "pc/RecentFiles.hpp"

#include <QGuiApplication>
#include <QMimeData>
#include <QClipboard>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <sys/statvfs.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>

namespace ravenspot {

namespace platform {
void trimHeap() { ::malloc_trim(0); }
} // namespace platform

void ShellBridge::initPlatform() {
    // Pano geçmişi: orkun-clipboard-daemon olaylarına abone (yalnızca bellek; daemon yoksa sessizce bekler)
    m_clipHistory->connectTo(ClipboardHistory::defaultSocketPath());
    connect(m_commandRunner, &CommandRunner::finished, this, [this](int code, bool crashed) {
        // Menü kapalıyken biten satır içi komutu bildir (argv; kabuk yok).
        if (m_layerShellWindow && m_layerShellWindow->isVisible()) return;
        const QString title = crashed ? QStringLiteral("Command stopped") : QStringLiteral("Command finished");
        QProcess::startDetached(QStringLiteral("notify-send"), {
            QStringLiteral("-a"), QStringLiteral("RavenSpot"),
            QStringLiteral("-i"), QStringLiteral("utilities-terminal"),
            title, cmd::firstLine(m_commandRunner->command()) + QStringLiteral(" — exit ") + QString::number(code)});
    });

    // Sync sleep inhibition state with KDE / FreeDesktop PowerManagement
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
        QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
        QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
        QStringLiteral("HasInhibitChanged"),
        this,
        SIGNAL(sleepInhibitedChanged(bool))
    );

    connect(this, &ShellBridge::sleepInhibitedChanged, this, [this](bool inhibited) {
        m_sleepInhibited = inhibited;
    });

    // Query initial power profile & sleep inhibit state once at startup (0ms overhead during open)
    QTimer::singleShot(0, this, [this]() {
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("net.hadess.PowerProfiles"),
            QStringLiteral("/net/hadess/PowerProfiles"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get")
        );
        msg << QStringLiteral("net.hadess.PowerProfiles") << QStringLiteral("ActiveProfile");
        QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(msg);
        if (reply.isValid()) {
            m_powerProfile = reply.value().variant().toString();
        }

        auto inMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("HasInhibit")
        );
        QDBusReply<bool> inReply = QDBusConnection::sessionBus().call(inMsg);
        if (inReply.isValid()) {
            m_sleepInhibited = inReply.value();
        }
    });
}

void ShellBridge::setSystemVolume(int vol) {
    const int clamped = std::clamp(vol, 0, 100);
    const QString cmd = QStringLiteral("wpctl set-volume @DEFAULT_AUDIO_SINK@ %1%").arg(clamped);
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

void ShellBridge::toggleSystemMute() {
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")});
}

void ShellBridge::applyColorscheme(const QString &name) {
    if (!name.isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {name});
    }
}

void ShellBridge::openPath(const QString &rawPath) {
    // "~/x" -> ev dizini (arama satırları yolu böyle taşır); kabuk kullanılmaz
    const QString path = rawPath.startsWith(QLatin1String("~/")) || rawPath == QLatin1String("~")
        ? QDir::homePath() + rawPath.mid(1) : rawPath;
    if (path.isEmpty()) return;
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"))) {
        QDesktopServices::openUrl(QUrl(path));
        return;
    }

    const QFileInfo fi(path);
    if (fi.isDir()) {
        const QString folderName = fi.fileName().toLower();
        if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("dolphin"), {fi.absoluteFilePath()});
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
        activateDolphinWindow(folderName);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ShellBridge::openContainingFolder(const QString &path) {
    if (path.isEmpty()) return;
    const QFileInfo fi(path);
    const QString folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    const QString folderName = QDir(folder).dirName().toLower();

    if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
        if (!fi.isDir() && fi.exists()) {
            QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), fi.absoluteFilePath()});
        } else {
            QProcess::startDetached(QStringLiteral("dolphin"), {folder});
        }
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }

    // Unminimize and bring open Dolphin window to the front
    activateDolphinWindow(folderName);
}

void ShellBridge::activateDolphinWindow(const QString &folderHint) {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Match")
    );
    msg << QString(); // query all open windows

    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.arguments().isEmpty()) return;

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return;

    arg.beginArray();
    QString bestMatchId;
    QString fallbackDolphinId;
    const QString hintLow = folderHint.toLower().trimmed();

    while (!arg.atEnd()) {
        arg.beginStructure();
        QString id;
        QString text;
        QString iconName;
        int type;
        double relevance;
        QVariantMap properties;
        arg >> id >> text >> iconName >> type >> relevance >> properties;
        arg.endStructure();

        const QString tLow = text.toLower();
        const QString iLow = iconName.toLower();

        const bool isDolphin = iLow.contains(QStringLiteral("dolphin")) ||
                               iLow.contains(QStringLiteral("system-file-manager")) ||
                               tLow.contains(QStringLiteral("dolphin"));

        if (isDolphin) {
            if (fallbackDolphinId.isEmpty()) {
                fallbackDolphinId = id;
            }
            if (!hintLow.isEmpty() && tLow.contains(hintLow)) {
                bestMatchId = id;
                break;
            }
        }
    }
    arg.endArray();

    const QString targetId = !bestMatchId.isEmpty() ? bestMatchId : fallbackDolphinId;
    if (!targetId.isEmpty()) {
        auto runMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/WindowsRunner"),
            QStringLiteral("org.kde.krunner1"),
            QStringLiteral("Run")
        );
        runMsg << targetId << QString();
        QDBusConnection::sessionBus().call(runMsg);
    }
}

void ShellBridge::launchApp(const QString &desktop) {
    if (m_appIndexer) {
        m_appIndexer->launchApp(desktop);
        return;
    }

    if (desktop.trimmed().isEmpty()) return;

    QString cmd = desktop;
    cmd.remove(QRegularExpression(QStringLiteral("%[uUfFdiDnvm]")));
    cmd = cmd.trimmed();
    if (cmd.isEmpty()) return;

    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

// KWin WindowsRunner Match yanıtını QML JSON satırlarına dönüştürür.
static QString windowMatchReplyToJson(const QDBusMessage &reply) {
    if (reply.arguments().isEmpty()) return QStringLiteral("[]");

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return QStringLiteral("[]");

    QJsonArray items;
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QString id;
        QString text;
        QString iconName;
        int type;
        double relevance;
        QVariantMap properties;
        arg >> id >> text >> iconName >> type >> relevance >> properties;
        arg.endStructure();

        QJsonObject obj;
        obj[QStringLiteral("id")] = id;
        obj[QStringLiteral("title")] = text;
        obj[QStringLiteral("iconName")] = iconName;
        items.append(obj);
    }
    arg.endArray();

    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
}

static QDBusMessage windowMatchMessage(const QString &q) {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Match")
    );
    msg << q;
    return msg;
}

QString ShellBridge::searchWindows(const QString &query) const {
    const QString q = query.trimmed();
    if (q.isEmpty()) return QStringLiteral("[]");
    return windowMatchReplyToJson(QDBusConnection::sessionBus().call(windowMatchMessage(q)));
}

void ShellBridge::requestWindowSearch(const QString &query, int requestId) {
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        emit windowSearchFinished(requestId, query, QStringLiteral("[]"));
        return;
    }
    // UI thread'ini bloklamadan KWin'e sor; eski yanıtlar QML'de requestId ile elenir.
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(windowMatchMessage(q), 500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, query, requestId](QDBusPendingCallWatcher *w) {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        emit windowSearchFinished(requestId, query,
            reply.type() == QDBusMessage::ReplyMessage ? windowMatchReplyToJson(reply) : QStringLiteral("[]"));
    });
}

bool ShellBridge::activateWindow(const QString &matchId) {
    if (matchId.isEmpty()) return false;

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/WindowsRunner"),
        QStringLiteral("org.kde.krunner1"),
        QStringLiteral("Run")
    );
    msg << matchId << QString();
    auto reply = QDBusConnection::sessionBus().call(msg);
    return reply.type() != QDBusMessage::ErrorMessage;
}

void ShellBridge::editDesktopFile(const QString &desktop) {
    if (desktop.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("xdg-open"), {desktop});
}

void ShellBridge::openInDiscover(const QString &desktop) {
    if (desktop.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("plasma-discover"), {QStringLiteral("--app"), desktop});
}

static QString resolveTerminal(); // aşağıda tanımlı

void ShellBridge::openInTerminal(const QString &path) {
    const QString targetDir = files::resolveTerminalDir(path, QDir::homePath());
    // Yol tek argv öğesi: eskiden "cd \"<yol>\"" metni /bin/sh'ta genişliyordu.
    QProcess::startDetached(resolveTerminal(), files::terminalInDirArgs(targetDir), targetDir);
}

void ShellBridge::extractHere(const QString &path) {
    QProcess::startDetached(QStringLiteral("ark"), files::extractArgs(path), QFileInfo(path).absolutePath());
}

void ShellBridge::compressPath(const QString &path) {
    QProcess::startDetached(QStringLiteral("ark"), files::compressArgs(path), QFileInfo(path).absolutePath());
}

QString ShellBridge::openWithApps(const QString &path) const {
    if (!m_desktopAppsLoaded) {
        m_desktopAppsLoaded = true;
        QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        dirs << QStringLiteral("/var/lib/flatpak/exports/share/applications")
             << QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications");
        QStringList seen;
        for (const QString &d : dirs) {
            for (const QFileInfo &fi : QDir(d).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
                if (seen.contains(fi.fileName())) continue; // XDG önceliği: ilk bulunan kazanır
                seen << fi.fileName();
                m_desktopApps << files::parseDesktopFile(fi.absoluteFilePath());
            }
        }
    }
    // Sistem varsayılanı (xdg-mime); kullanıcı eylemiyle tetiklenir, kısa zaman aşımı
    QString defaultId;
    QProcess q;
    q.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"),
                                         files::properties(path).value(QStringLiteral("mime")).toString()});
    if (q.waitForFinished(800)) defaultId = QString::fromUtf8(q.readAllStandardOutput()).trimmed();
    else q.kill();
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(
        files::openWithApps(path, m_desktopApps, defaultId))).toJson(QJsonDocument::Compact));
}

void ShellBridge::openWith(const QString &path, const QString &desktopFile) {
    if (path.isEmpty() || desktopFile.isEmpty()) return;
    // gio launch .desktop Exec alan kodlarını (%f/%u) doğru genişletir; kendi scope'unda başlar
    QProcess::startDetached(QStringLiteral("systemd-run"), {
        QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"),
        QStringLiteral("gio"), QStringLiteral("launch"), desktopFile, path});
}

bool ShellBridge::isAutostart(const QString &desktopFile) const {
    return files::isAutostart(desktopFile);
}

bool ShellBridge::setAutostart(const QString &desktopFile, bool enabled) {
    return files::setAutostart(desktopFile, enabled);
}

bool ShellBridge::canUninstall(const QString &desktopFile) const {
    // Menü açılışında pacman sorgusu yapmamak için yola göre karar; sahip tıklamada çözülür
    return desktopFile.contains(QLatin1String("/flatpak/exports/share/applications/"))
        || desktopFile.startsWith(QLatin1String("/usr/"));
}

void ShellBridge::uninstallApp(const QString &desktopFile) {
    const QString cmd = files::uninstallCommand(desktopFile, [](const QString &file) {
        QProcess p;
        p.start(QStringLiteral("pacman"), {QStringLiteral("-Qqo"), file});
        if (!p.waitForFinished(3000)) { p.kill(); return QString(); }
        return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
    });
    if (cmd.isEmpty()) {
        QProcess::startDetached(QStringLiteral("notify-send"), {QStringLiteral("-a"), QStringLiteral("RavenSpot"),
            QStringLiteral("-i"), QStringLiteral("dialog-information"),
            QStringLiteral("Cannot uninstall"), QStringLiteral("No package owns ") + QFileInfo(desktopFile).fileName()});
        return;
    }
    // Komut geçmişine yazma (">" önerilerinde "kaldır" tekrar çıkmasın); paket yöneticisi onay sorar
    QProcess::startDetached(resolveTerminal(), cmd::terminalArgs(cmd), QDir::homePath());
}

static QString resolveTerminal() {
    QString term = QString::fromUtf8(qgetenv("TERMINAL"));
    if (term.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("konsole"), QStringLiteral("kitty"), QStringLiteral("alacritty"),
            QStringLiteral("foot"), QStringLiteral("wezterm"), QStringLiteral("xterm")
        };
        for (const auto &c : candidates) {
            if (!QStandardPaths::findExecutable(c).isEmpty()) return c;
        }
    }
    return term.isEmpty() ? QStringLiteral("xterm") : term;
}

void ShellBridge::runCommandInTerminal(const QString &command) {
    if (command.trimmed().isEmpty()) return;
    // Komut tek argv öğesi olarak bash'e gider: RavenSpot hiçbir şeyi genişletmez.
    QProcess::startDetached(resolveTerminal(), cmd::terminalArgs(command), QDir::homePath());
    m_commandHistory->add(command);
}

void ShellBridge::runInTerminal(const QString &desktopOrCmd) {
    if (desktopOrCmd.trimmed().isEmpty()) return;

    QString term = QString::fromUtf8(qgetenv("TERMINAL"));
    if (term.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("konsole"),
            QStringLiteral("kitty"),
            QStringLiteral("alacritty"),
            QStringLiteral("foot"),
            QStringLiteral("wezterm"),
            QStringLiteral("xterm")
        };
        for (const auto &c : candidates) {
            if (!QStandardPaths::findExecutable(c).isEmpty()) {
                term = c;
                break;
            }
        }
    }
    if (term.isEmpty()) term = QStringLiteral("xterm");

    QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), desktopOrCmd});
}

void ShellBridge::runDetached(const QString &command) {
    if (command.trimmed().isEmpty()) return;
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

QString ShellBridge::bash(const QString &script) {
    if (script.trimmed().isEmpty()) return QString();

    QProcess proc;
    proc.start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), script});
    if (proc.waitForFinished(1500)) {
        return QString::fromUtf8(proc.readAllStandardOutput());
    }
    proc.kill();
    return QString();
}

QString ShellBridge::detectGpuVendor() const {
    QDir driDir(QStringLiteral("/sys/class/drm"));
    const QStringList cards = driDir.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &c : cards) {
        if (c.contains(QLatin1Char('-'))) continue;
        QFile vendorFile(QStringLiteral("/sys/class/drm/%1/device/vendor").arg(c));
        if (vendorFile.open(QIODevice::ReadOnly)) {
            const QString v = QString::fromUtf8(vendorFile.readAll()).trimmed().toLower();
            if (v == QStringLiteral("0x1002")) return QStringLiteral("amd");
            if (v == QStringLiteral("0x10de")) return QStringLiteral("nvidia");
            if (v == QStringLiteral("0x8086")) return QStringLiteral("intel");
        }
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty()) {
        return QStringLiteral("nvidia");
    }
    return QStringLiteral("amd");
}

QString ShellBridge::getNashiPath() const {
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("nashi"));
    if (!inPath.isEmpty()) return inPath;

    const QString localBin = QDir::homePath() + QStringLiteral("/.local/bin/nashi");
    if (QFile::exists(localBin)) return localBin;

    const QString localShare = QDir::homePath() + QStringLiteral("/.local/share/nashi/nashi");
    if (QFile::exists(localShare)) return localShare;

    const QString desktopPath = QStringLiteral("/home/dokuro/Masaüstü/orkunprogram/nashi-linux/nashi");
    if (QFile::exists(desktopPath)) return desktopPath;

    return QString();
}

void ShellBridge::installPackage(const QString &packageName, const QString &customCommand) {
    if (packageName == QStringLiteral("nashi") || packageName == QStringLiteral("nashi-app") || customCommand == QStringLiteral("install-nashi")) {
        installNashi();
        return;
    }

    QString cmd = customCommand.trimmed();
    if (cmd.isEmpty()) {
        cmd = QStringLiteral("sudo pacman -S --noconfirm %1 || yay -S --noconfirm %1").arg(packageName);
    }

    const QString script = QStringLiteral("%1; echo; echo 'Kurulum tamamlandı. Kapatmak için Enter tuşuna basın...'; read -r _").arg(cmd);
    runInTerminal(script);
}

void ShellBridge::installNashi() {
    const QString script = QStringLiteral(
        "echo '=== Nashi Kurulumu Başlatılıyor (GitHub: cdokuro/nashi-app) ==='; "
        "mkdir -p ~/.local/share && cd ~/.local/share && "
        "if [ -d nashi/.git ]; then "
        "  echo 'Mevcut kopya güncelleniyor...'; cd nashi && git pull; "
        "else "
        "  echo 'GitHub deposu klonlanıyor...'; rm -rf nashi && git clone https://github.com/cdokuro/nashi-app.git nashi && cd nashi; "
        "fi && "
        "chmod +x nashi && "
        "mkdir -p ~/.local/bin && ln -sf ~/.local/share/nashi/nashi ~/.local/bin/nashi && "
        "echo 'Nashi başarıyla kuruldu (~/.local/bin/nashi)!'; "
        "notify-send 'Nashi' 'Kurulum tamamlandı ve menüye bağlandı!' 2>/dev/null || true; "
        "echo; echo 'Devam etmek için Enter tuşuna basın...'; read -r _"
    );
    runInTerminal(script);
}

// ── 6. Power & System Actions ──────────────────────────────

void ShellBridge::powerOff() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logoutAndShutdown")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    }
}

void ShellBridge::rebootSystem() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logoutAndReboot")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    }
}

void ShellBridge::lockScreen() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("Lock")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    }
}

void ShellBridge::suspendSystem() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.PowerManagement"),
        QStringLiteral("/org/freedesktop/PowerManagement"),
        QStringLiteral("org.freedesktop.PowerManagement"),
        QStringLiteral("Suspend")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
    }
}

void ShellBridge::logoutUser() {
    // org.kde.Shutdown.logout() argümansızdır. Eskiden (1,2,2) gönderiliyordu:
    // imza eşleşmediği için "No such method" dönüyor ve oturum
    // `loginctl terminate-session` ile kaydedilmeden, uygulamalar kapanmadan
    // öldürülüyordu. Bu çağrı oturumu düzgün kapatır (ayarlıysa kaydeder).
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logout")
    );
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        if (w->isError()) {
            qWarning() << "logout failed:" << w->error().message() << "- falling back to KDE logout prompt";
            // Asla sert sonlandırma yapma: KDE'nin onay penceresini göster.
            QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptLogout")));
        }
        w->deleteLater();
    });
}

void ShellBridge::saveSession() {
    // KDE "Save Session": açık uygulamaları kaydeder; oturum açılışında
    // "Restore manually saved session" (ksmserverrc loginMode=restoreSavedSession)
    // ile geri yüklenir. İstemciler tek tek kaydettiği için async çağrılır.
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.ksmserver"),
        QStringLiteral("/KSMServer"),
        QStringLiteral("org.kde.KSMServerInterface"),
        QStringLiteral("saveCurrentSession")
    );
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        const bool ok = !w->isError();
        if (!ok) qWarning() << "saveCurrentSession failed:" << w->error().message();
        QProcess::startDetached(QStringLiteral("notify-send"), {
            QStringLiteral("-a"), QStringLiteral("RavenSpot"),
            QStringLiteral("-i"), ok ? QStringLiteral("document-save") : QStringLiteral("dialog-error"),
            ok ? QStringLiteral("Session saved") : QStringLiteral("Session could not be saved"),
            ok ? QStringLiteral("Open apps will be restored at next login.") : w->error().message()
        });
        w->deleteLater();
    });
}

void ShellBridge::restartPlasma() {
    QProcess::startDetached(QStringLiteral("/bin/sh"), {
        QStringLiteral("-c"),
        QStringLiteral("systemctl --user restart plasma-plasmashell.service || (kquitapp6 plasmashell || killall plasmashell; kstart plasmashell)")
    });
}


bool ShellBridge::setPowerProfile(const QString &profile) {
    if (profile != QStringLiteral("power-saver") &&
        profile != QStringLiteral("balanced") &&
        profile != QStringLiteral("performance")) {
        return false;
    }

    m_powerProfile = profile;

    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("net.hadess.PowerProfiles"),
        QStringLiteral("/net/hadess/PowerProfiles"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Set")
    );
    msg << QStringLiteral("net.hadess.PowerProfiles")
        << QStringLiteral("ActiveProfile")
        << QVariant::fromValue(QDBusVariant(profile));
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        QProcess::startDetached(QStringLiteral("powerprofilesctl"), {QStringLiteral("set"), profile});
    }

    emit powerProfileChanged(m_powerProfile);
    return true;
}

bool ShellBridge::setSleepInhibited(bool inhibit) {
    if (inhibit) {
        if (m_sleepInhibitCookie != 0) return true;

        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("Inhibit")
        );
        msg << QStringLiteral("RavenSpot") << QStringLiteral("Manually Block Sleep and Screen Locking");
        QDBusReply<uint> reply = QDBusConnection::sessionBus().call(msg);
        if (reply.isValid() && reply.value() != 0) {
            m_sleepInhibitCookie = reply.value();
            m_sleepInhibited = true;
            emit sleepInhibitedChanged(true);
            return true;
        }
        return false;
    } else {
        if (m_sleepInhibitCookie == 0) return true;

        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
            QStringLiteral("UnInhibit")
        );
        msg << m_sleepInhibitCookie;
        QDBusConnection::sessionBus().call(msg);
        m_sleepInhibitCookie = 0;
        m_sleepInhibited = false;
        emit sleepInhibitedChanged(false);
        return true;
    }
}

QString ShellBridge::recentFiles(const QString &query) const {
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(
        pc::recentFiles(pc::defaultXbelPath(), query))).toJson(QJsonDocument::Compact));
}

QString ShellBridge::listProcesses(const QString &query) const {
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(
        pc::listProcesses(query, QStringLiteral("/proc"), ::getuid()))).toJson(QJsonDocument::Compact));
}

bool ShellBridge::forceKillProcess(const QString &pid) {
    bool ok = false;
    const int p = pid.trimmed().toInt(&ok);
    if (!ok || p <= 1) return false;
    QProcess::startDetached(QStringLiteral("kill"), {QStringLiteral("-9"), QString::number(p)});
    return true;
}

bool ShellBridge::killProcess(const QString &pid) {
    if (pid.trimmed().isEmpty()) return false;
    bool ok = false;
    int p = pid.toInt(&ok);
    if (!ok || p <= 1) return false;
    QProcess::startDetached(QStringLiteral("kill"), {QStringLiteral("-15"), QString::number(p)});
    return true;
}

QVariantMap ShellBridge::getSysMonitorInfo() const {
    QVariantMap map;
    map[QStringLiteral("ram_text")] = QStringLiteral("--");
    map[QStringLiteral("ram_pct")] = 0;
    map[QStringLiteral("disk_text")] = QStringLiteral("--");
    map[QStringLiteral("disk_pct")] = 0;
    map[QStringLiteral("temp")] = QStringLiteral("--");
    map[QStringLiteral("uptime")] = QStringLiteral("--");

    // RAM calculation via /proc/meminfo
    QFile memFile(QStringLiteral("/proc/meminfo"));
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        unsigned long long totalMem = 0;
        unsigned long long availMem = 0;
        while (!memFile.atEnd()) {
            const QByteArray line = memFile.readLine();
            if (line.startsWith("MemTotal:")) {
                const auto parts = line.simplified().split(' ');
                if (parts.size() >= 2) totalMem = parts[1].toULongLong();
            } else if (line.startsWith("MemAvailable:")) {
                const auto parts = line.simplified().split(' ');
                if (parts.size() >= 2) availMem = parts[1].toULongLong();
            }
        }
        memFile.close();
        if (totalMem > 0) {
            const unsigned long long usedMem = totalMem > availMem ? totalMem - availMem : 0;
            const int ramPct = static_cast<int>((usedMem * 100) / totalMem);
            map[QStringLiteral("ram_pct")] = ramPct;
            map[QStringLiteral("ram_text")] = QStringLiteral("%1 / %2 GB")
                .arg(usedMem / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(totalMem / (1024.0 * 1024.0), 0, 'f', 1);
        }
    }

    // Disk calculation via statvfs on "/"
    struct statvfs svfs{};
    if (statvfs("/", &svfs) == 0) {
        const unsigned long long total = svfs.f_blocks * svfs.f_frsize;
        const unsigned long long free = svfs.f_bavail * svfs.f_frsize;
        if (total > 0) {
            const unsigned long long used = total - free;
            const int diskPct = static_cast<int>((used * 100) / total);
            map[QStringLiteral("disk_pct")] = diskPct;
            map[QStringLiteral("disk_text")] = QStringLiteral("%1 / %2 GB")
                .arg(used / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0)
                .arg(total / (1024.0 * 1024.0 * 1024.0), 0, 'f', 0);
        }
    }

    // Thermal read
    QFile tempFile(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const int milliC = tempFile.readLine().trimmed().toInt();
        tempFile.close();
        if (milliC > 0) {
            map[QStringLiteral("temp")] = QStringLiteral("%1°C").arg(milliC / 1000);
        }
    }

    // Uptime read
    QFile uptimeFile(QStringLiteral("/proc/uptime"));
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const double secs = QString::fromUtf8(uptimeFile.readLine()).section(' ', 0, 0).toDouble();
        uptimeFile.close();
        const int hours = static_cast<int>(secs) / 3600;
        const int mins = (static_cast<int>(secs) % 3600) / 60;
        map[QStringLiteral("uptime")] = QStringLiteral("%1h %2m").arg(hours).arg(mins);
    }

    return map;
}

QVariantMap ShellBridge::getUpdateInfo() const {
    const QString stateDir = QDir::homePath() + QStringLiteral("/.local/state/arch-update");
    const QString trayFile = stateDir + QStringLiteral("/tray_icon");
    const QString updatesFile = stateDir + QStringLiteral("/last_updates_check");
    const QString notifFile = QStringLiteral("/run/user/%1/arch-update/notif_param").arg(getuid());

    bool hasUpdates = false;
    int count = 0;
    QString trayVal;

    QFile fTray(trayFile);
    if (fTray.open(QIODevice::ReadOnly | QIODevice::Text)) {
        trayVal = QString::fromUtf8(fTray.readAll()).trimmed();
        fTray.close();
        if (trayVal.contains(QStringLiteral("updates-available"))) {
            hasUpdates = true;
        }
    }

    QFile fUpdates(updatesFile);
    if (fUpdates.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!fUpdates.atEnd()) {
            if (!fUpdates.readLine().trimmed().isEmpty()) {
                ++count;
            }
        }
        fUpdates.close();
        if (count > 0) {
            hasUpdates = true;
        }
    } else if (hasUpdates) {
        QFile fNotif(notifFile);
        if (fNotif.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString c = QString::fromUtf8(fNotif.readAll()).trimmed();
            fNotif.close();
            bool ok = false;
            int n = c.toInt(&ok);
            if (ok && n > 0) count = n;
        }
    }

    QString iconPath;
    QString tooltip;
    if (hasUpdates) {
        QString iconName = trayVal.contains(QStringLiteral("updates-available")) ? trayVal : QStringLiteral("cachy-update_updates-available-blue");
        iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/%1.svg").arg(iconName);
        if (!QFile::exists(iconPath)) {
            iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/cachy-update_updates-available-blue.svg");
        }
        QString cntStr = count > 0 ? QStringLiteral(" (%1)").arg(count) : QString();
        tooltip = QStringLiteral("Updates Available%1 — Click to Update").arg(cntStr);
    } else {
        QString iconName = (!trayVal.isEmpty() && !trayVal.contains(QStringLiteral("updates-available"))) ? trayVal : QStringLiteral("cachy-update-blue");
        iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/%1.svg").arg(iconName);
        if (!QFile::exists(iconPath)) {
            iconPath = QStringLiteral("/usr/share/icons/hicolor/scalable/apps/cachy-update-blue.svg");
        }
        tooltip = QStringLiteral("System Up to Date (CachyOS)");
    }

    QVariantMap map;
    map[QStringLiteral("has_updates")] = hasUpdates;
    map[QStringLiteral("count")] = count;
    map[QStringLiteral("icon_path")] = iconPath;
    map[QStringLiteral("tooltip")] = tooltip;
    return map;
}

void ShellBridge::launchUpdate() {
    runInTerminal(QStringLiteral("cachy-update || yay -Syu || sudo pacman -Syu"));
}

void ShellBridge::openSystemMonitor(const QString &section) {
    if (section == QStringLiteral("gpu")) {
        if (!QStandardPaths::findExecutable(QStringLiteral("lact")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("lact"), {});
            return;
        }
    } else if (section == QStringLiteral("cpu")) {
        if (!QStandardPaths::findExecutable(QStringLiteral("cpu-x")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("cpu-x"), {});
            return;
        }
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("plasma-systemmonitor")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-systemmonitor"), {});
    } else if (!QStandardPaths::findExecutable(QStringLiteral("btop")).isEmpty()) {
        runInTerminal(QStringLiteral("btop"));
    } else if (!QStandardPaths::findExecutable(QStringLiteral("htop")).isEmpty()) {
        runInTerminal(QStringLiteral("htop"));
    }
}

void ShellBridge::updateTelemetry() {
    // Read /proc/stat for CPU
    QFile statFile(QStringLiteral("/proc/stat"));
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray firstLine = statFile.readLine();
        statFile.close();

        unsigned long long user = 0, nice = 0, sys = 0, idle = 0;
        if (sscanf(firstLine.constData(), "cpu %llu %llu %llu %llu", &user, &nice, &sys, &idle) == 4) {
            if (m_lastTotalUser != 0 || m_lastTotalSys != 0 || m_lastTotalIdle != 0) {
                const unsigned long long diffUser = (user > m_lastTotalUser) ? (user - m_lastTotalUser) : 0;
                const unsigned long long diffNice = (nice > m_lastTotalUserLow) ? (nice - m_lastTotalUserLow) : 0;
                const unsigned long long diffSys = (sys > m_lastTotalSys) ? (sys - m_lastTotalSys) : 0;
                const unsigned long long diffIdle = (idle > m_lastTotalIdle) ? (idle - m_lastTotalIdle) : 0;
                const unsigned long long totalDiff = diffUser + diffNice + diffSys + diffIdle;
                if (totalDiff > 0) {
                    m_lastCpuPercent = static_cast<int>(((diffUser + diffNice + diffSys) * 100) / totalDiff);
                }
            }
            m_lastTotalUser = user;
            m_lastTotalUserLow = nice;
            m_lastTotalSys = sys;
            m_lastTotalIdle = idle;
        }
    }

    // Try reading AMD/Intel GPU busy percent from sysfs
    QFile gpuFile(QStringLiteral("/sys/class/drm/card0/device/gpu_busy_percent"));
    if (gpuFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastGpuPercent = gpuFile.readLine().trimmed().toInt();
        gpuFile.close();
    }

    const int vol = m_audioEngine ? m_audioEngine->volume() : 10;
    const bool muted = m_audioEngine ? m_audioEngine->isMuted() : false;

    emit statsUpdated(m_lastCpuPercent, m_lastGpuPercent, vol, muted);

    QVariantMap metrics = getSysMonitorInfo();
    metrics[QStringLiteral("cpu")] = m_lastCpuPercent;
    metrics[QStringLiteral("gpu")] = m_lastGpuPercent;
    emit telemetryUpdated(metrics);
}

} // namespace ravenspot
