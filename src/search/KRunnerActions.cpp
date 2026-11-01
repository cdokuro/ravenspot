#include "KRunnerActions.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <csignal>
#include <algorithm>

namespace ravenspot {

KRunnerActions::KRunnerActions() {
    initRegistry();
}

void KRunnerActions::initRegistry() {
    m_actionRegistry = {
        {
            QStringLiteral("shutdown"), QStringLiteral("__sys_poweroff__"),
            QStringLiteral("Shutdown"), QStringLiteral("Power off the computer immediately"),
            QStringLiteral("󰐥"), QStringLiteral("system-shutdown"), QStringLiteral("#ff5252"),
            QStringLiteral("qdbus6 org.kde.Shutdown /Shutdown logoutAndShutdown"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("poweroff"),
            -100,
            {QStringLiteral("shutdown"), QStringLiteral("kapat"), QStringLiteral("poweroff"),
             QStringLiteral("power off"), QStringLiteral("sistemi kapat"), QStringLiteral("bilgisayari kapat")},
            true
        },
        {
            QStringLiteral("reboot"), QStringLiteral("__sys_reboot__"),
            QStringLiteral("Reboot"), QStringLiteral("Restart the computer"),
            QStringLiteral("󰜉"), QStringLiteral("system-reboot"), QStringLiteral("#ffa726"),
            QStringLiteral("qdbus6 org.kde.Shutdown /Shutdown logoutAndReboot"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("reboot"),
            -100,
            {QStringLiteral("reboot"), QStringLiteral("restart"), QStringLiteral("yeniden başlat"),
             QStringLiteral("yeniden baslat"), QStringLiteral("yeniden"), QStringLiteral("reset")},
            true
        },
        {
            QStringLiteral("lock"), QStringLiteral("__sys_lock__"),
            QStringLiteral("Lock Screen"), QStringLiteral("Lock current session immediately"),
            QStringLiteral("󰌾"), QStringLiteral("system-lock-screen"), QStringLiteral("#42a5f5"),
            QStringLiteral("loginctl lock-session"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("lock"),
            -100,
            {QStringLiteral("lock"), QStringLiteral("kilitle"), QStringLiteral("lock screen"),
             QStringLiteral("ekranı kilitle"), QStringLiteral("ekrani kilitle"), QStringLiteral("kilit")},
            true
        },
        {
            QStringLiteral("suspend"), QStringLiteral("__sys_sleep__"),
            QStringLiteral("Suspend (Sleep)"), QStringLiteral("Suspend system to RAM"),
            QStringLiteral("󰒲"), QStringLiteral("system-suspend"), QStringLiteral("#26c6da"),
            QStringLiteral("systemctl suspend"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("sleep"),
            -100,
            {QStringLiteral("suspend"), QStringLiteral("sleep"), QStringLiteral("uyut"),
             QStringLiteral("uyku"), QStringLiteral("askıya al"), QStringLiteral("askiya al")},
            true
        },
        {
            QStringLiteral("hibernate"), QStringLiteral("__sys_hibernate__"),
            QStringLiteral("Hibernate"), QStringLiteral("Save session and suspend to disk"),
            QStringLiteral("󰤁"), QStringLiteral("system-suspend-hibernate"), QStringLiteral("#00bcd4"),
            QStringLiteral("systemctl hibernate"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("hibernate"),
            -100,
            {QStringLiteral("hibernate"), QStringLiteral("hazırda beklet"), QStringLiteral("hazirda beklet"),
             QStringLiteral("derin uyku")},
            true
        },
        {
            QStringLiteral("logout"), QStringLiteral("__sys_logout__"),
            QStringLiteral("Log Out"), QStringLiteral("End current user session"),
            QStringLiteral("󰍃"), QStringLiteral("system-log-out"), QStringLiteral("#ab47bc"),
            QStringLiteral("qdbus6 org.kde.Shutdown /Shutdown logout"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("logout"),
            -100,
            {QStringLiteral("logout"), QStringLiteral("çıkış"), QStringLiteral("cikis"),
             QStringLiteral("oturumu kapat"), QStringLiteral("oturum kapat"), QStringLiteral("sign out")},
            true
        },
        {
            QStringLiteral("restart_plasma"), QStringLiteral("__sys_restart_plasma__"),
            QStringLiteral("Restart KDE Plasma"), QStringLiteral("Refresh and restart desktop shell"),
            QStringLiteral("󰑐"), QStringLiteral("system-reboot"), QStringLiteral("#26a69a"),
            QStringLiteral("systemctl --user restart plasma-plasmashell"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("restart_plasma"),
            -50,
            {QStringLiteral("restart plasma"), QStringLiteral("plasma restart"), QStringLiteral("plasma yenile"),
             QStringLiteral("kwin restart"), QStringLiteral("plasma yeniden başlat"), QStringLiteral("plasma yeniden baslat")},
            false
        },
        {
            QStringLiteral("screensaver"), QStringLiteral("system.screensaver"),
            QStringLiteral("Screensaver"), QStringLiteral("Start screensaver / lock session"),
            QStringLiteral("󱄄"), QStringLiteral("preferences-desktop-screensaver"), QStringLiteral("#81c784"),
            QStringLiteral("loginctl lock-session"),
            QStringLiteral("Sistem"), QStringLiteral("sys-action"), QStringLiteral("screensaver"),
            -100,
            {QStringLiteral("screensaver"), QStringLiteral("screen saver"), QStringLiteral("ekran koruyucu")},
            true
        }
    };
}

QString KRunnerActions::normalizeTurkish(const QString &input) {
    QString s = input.toLower();
    s.replace(QStringLiteral("ı"), QStringLiteral("i"));
    s.replace(QStringLiteral("İ"), QStringLiteral("i"));
    s.replace(QStringLiteral("ğ"), QStringLiteral("g"));
    s.replace(QStringLiteral("ü"), QStringLiteral("u"));
    s.replace(QStringLiteral("ş"), QStringLiteral("s"));
    s.replace(QStringLiteral("ö"), QStringLiteral("o"));
    s.replace(QStringLiteral("ç"), QStringLiteral("c"));
    return s.trimmed();
}

std::vector<SearchResultItem> KRunnerActions::search(const QString &query) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return {};

    std::vector<SearchResultItem> results;

    // Check kill command: ^kill\s+(.+)$
    static const QRegularExpression killRegex(QStringLiteral("^kill\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
    const auto killMatch = killRegex.match(trimmed);
    if (killMatch.hasMatch()) {
        const QString filter = killMatch.captured(1).trimmed();
        return listMatchingProcesses(filter);
    }

    const QString normQuery = normalizeTurkish(trimmed);

    for (const auto &action : m_actionRegistry) {
        int bestMatchScore = 0;
        bool matched = false;

        for (const auto &kw : action.keywords) {
            const QString normKw = normalizeTurkish(kw);
            if (normKw == normQuery) {
                bestMatchScore = -99992;
                matched = true;
                break;
            }
            if (normKw.startsWith(normQuery) || normQuery.startsWith(normKw)) {
                bestMatchScore = -99990;
                matched = true;
            } else if (!matched && (normKw.contains(normQuery) || normQuery.contains(normKw))) {
                bestMatchScore = -99985;
                matched = true;
            }
        }

        if (matched) {
            SearchResultItem item;
            item.id = action.itemId;
            item.name = action.label;
            item.comment = action.detail;
            item.icon = action.iconGlyph;
            item.iconName = action.iconName;
            item.type = QStringLiteral("krunner");
            // Security: exec command remains the fixed command, never user input
            item.exec = action.execCommand;
            item.path = action.path;
            item.provider = action.provider;
            item.value = action.value;
            item.score = bestMatchScore;
            results.push_back(std::move(item));
        }
    }

    return results;
}

std::vector<SearchResultItem> KRunnerActions::getSystemGridActions() const {
    std::vector<SearchResultItem> grid;
    for (const auto &action : m_actionRegistry) {
        if (!action.isGridAction) continue;

        SearchResultItem item;
        item.id = action.itemId;
        item.name = action.label;
        item.comment = action.detail;
        item.icon = action.iconGlyph;
        item.iconName = action.iconName;
        item.type = QStringLiteral("krunner");
        item.exec = action.execCommand;
        item.path = action.path;
        item.provider = action.provider;
        item.value = action.value;
        item.score = action.score;
        grid.push_back(std::move(item));
    }
    return grid;
}

std::vector<SearchResultItem> KRunnerActions::listMatchingProcesses(const QString &filter) {
    std::vector<SearchResultItem> results;
    if (filter.trimmed().isEmpty()) return results;

    const QString lowerFilter = filter.trimmed().toLower();
    QDir procDir(QStringLiteral("/proc"));
    const QStringList pids = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const auto &pidStr : pids) {
        bool ok = false;
        int pid = pidStr.toInt(&ok);
        if (!ok || pid <= 1) continue;

        QFile commFile(QStringLiteral("/proc/") + pidStr + QStringLiteral("/comm"));
        if (!commFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        const QString comm = QString::fromUtf8(commFile.readAll()).trimmed();
        commFile.close();

        if (comm.toLower().contains(lowerFilter)) {
            SearchResultItem item;
            item.id = QStringLiteral("kill:") + pidStr;
            item.name = QStringLiteral("Kill ") + comm + QStringLiteral(" (PID: ") + pidStr + QStringLiteral(")");
            item.comment = QStringLiteral("Süreci sonlandır (SIGTERM)");
            item.icon = QStringLiteral("󰅚");
            item.iconName = QStringLiteral("process-stop");
            item.type = QStringLiteral("krunner");
            item.exec = QStringLiteral("kill -15 ") + pidStr;
            item.path = QStringLiteral("Süreçler");
            item.provider = QStringLiteral("sys-action");
            item.value = pidStr;
            item.score = -99990;
            results.push_back(std::move(item));

            if (results.size() >= 15) break;
        }
    }

    return results;
}

bool KRunnerActions::killProcess(const QString &pidStr) {
    bool ok = false;
    const pid_t pid = static_cast<pid_t>(pidStr.toInt(&ok));
    if (!ok || pid <= 1) return false;
    return ::kill(pid, SIGTERM) == 0;
}

bool KRunnerActions::executeAction(const QString &actionId, const QString &param) {
    if (actionId == QLatin1String("poweroff") || actionId == QLatin1String("shutdown")) {
        QDBusInterface kdeShutdown(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                   QStringLiteral("org.kde.Shutdown"), QDBusConnection::sessionBus());
        if (kdeShutdown.isValid()) {
            kdeShutdown.call(QStringLiteral("logoutAndShutdown"));
            return true;
        }
        return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    }

    if (actionId == QLatin1String("reboot")) {
        QDBusInterface kdeShutdown(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                   QStringLiteral("org.kde.Shutdown"), QDBusConnection::sessionBus());
        if (kdeShutdown.isValid()) {
            kdeShutdown.call(QStringLiteral("logoutAndReboot"));
            return true;
        }
        return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    }

    if (actionId == QLatin1String("lock") || actionId == QLatin1String("screensaver")) {
        QDBusInterface screenSaver(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                   QStringLiteral("org.freedesktop.ScreenSaver"), QDBusConnection::sessionBus());
        if (screenSaver.isValid()) {
            screenSaver.call(QStringLiteral("Lock"));
            return true;
        }
        return QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    }

    if (actionId == QLatin1String("sleep") || actionId == QLatin1String("suspend")) {
        return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
    }

    if (actionId == QLatin1String("hibernate")) {
        return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("hibernate")});
    }

    if (actionId == QLatin1String("logout")) {
        QDBusInterface kdeShutdown(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                   QStringLiteral("org.kde.Shutdown"), QDBusConnection::sessionBus());
        if (kdeShutdown.isValid()) {
            kdeShutdown.call(QStringLiteral("logout"));
            return true;
        }
        return QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), QStringLiteral("")});
    }

    if (actionId == QLatin1String("restart_plasma")) {
        return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("plasma-plasmashell")});
    }

    if (actionId == QLatin1String("kill")) {
        return killProcess(param);
    }

    return false;
}

} // namespace ravenspot
