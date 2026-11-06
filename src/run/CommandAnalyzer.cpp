#include "run/CommandAnalyzer.hpp"

#include <QRegularExpression>

namespace ravenspot::cmd {

namespace {

const QRegularExpression &privilegePrefix() {
    static const QRegularExpression re(QStringLiteral("^(sudo|doas|pkexec)(\\s|$)"));
    return re;
}

// Kabuk sözdizimi: bunlardan biri varsa metin açıkça bir komuttur.
const QRegularExpression &shellSyntax() {
    static const QRegularExpression re(QStringLiteral(
        "\\||&&|;|\\$\\(|`|\\n|(^|\\s)\\d?[<>]{1,2}\\s*\\S"));
    return re;
}

QString stripPrivilege(const QString &command) {
    QString s = command.trimmed();
    while (true) {
        const auto m = privilegePrefix().match(s);
        if (!m.hasMatch()) return s;
        s = s.mid(m.capturedLength()).trimmed();
    }
}

QString firstToken(const QString &command) {
    const QString s = command.trimmed();
    const qsizetype ws = s.indexOf(QRegularExpression(QStringLiteral("\\s")));
    return ws < 0 ? s : s.left(ws);
}

} // namespace

Signal detect(const QString &text, const ExecutableCheck &isExecutable) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return Signal::None;
    if (privilegePrefix().match(t).hasMatch()) return Signal::Strong;
    if (shellSyntax().match(t).hasMatch()) return Signal::Strong;

    const QStringList tokens = t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (t.startsWith(QLatin1String("./")) || t.startsWith(QLatin1String("~/")) || t.startsWith(QLatin1Char('/'))) {
        // Tek başına bir yol ("~/Downloads") önce klasör/dosya sonucu olarak kalsın
        return tokens.size() >= 2 ? Signal::Strong : Signal::Weak;
    }
    // Tek kelime asla kendiliğinden komut sayılmaz ("firefox" uygulama araması kalır).
    if (tokens.size() >= 2 && isExecutable && isExecutable(tokens.first())) return Signal::Weak;
    return Signal::None;
}

namespace {

QString unquote(QString t) {
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\''))))) {
        t = t.mid(1, t.size() - 2);
    }
    return t;
}

// rm hedefi: kök, ev dizini, joker/bulunulan dizin, /home/<kullanıcı> ya da
// üst düzey bir sistem dizini (ve bunların "/*" biçimleri). Alt yollar değil.
bool isDangerousRmTarget(const QString &raw) {
    const QString t = unquote(raw);
    static const QStringList exact = {
        QStringLiteral("/"), QStringLiteral("/*"), QStringLiteral("~"), QStringLiteral("~/"), QStringLiteral("~/*"),
        QStringLiteral("$HOME"), QStringLiteral("$HOME/"), QStringLiteral("$HOME/*"),
        QStringLiteral("${HOME}"), QStringLiteral("${HOME}/"), QStringLiteral("${HOME}/*"),
        QStringLiteral("*"), QStringLiteral("."), QStringLiteral("./"), QStringLiteral("./*"),
        QStringLiteral(".."), QStringLiteral("../"), QStringLiteral("../*")};
    if (exact.contains(t)) return true;
    static const QRegularExpression homeUser(QStringLiteral("^/home/[^/\\s]+/?\\*?$"));
    static const QRegularExpression systemDir(QStringLiteral(
        "^/(usr|etc|bin|sbin|boot|var|home|lib|lib64|opt|root|srv|sys|proc|dev|mnt|run)(/\\*?)?$"));
    return homeUser.match(t).hasMatch() || systemDir.match(t).hasMatch();
}

bool isDangerousRm(const QStringList &tokens) {
    qsizetype i = 0;
    if (i >= tokens.size()) return false;
    const QString prog = tokens.at(i);
    if (prog != QLatin1String("rm") && !prog.endsWith(QLatin1String("/rm"))) return false;
    bool recursive = false;
    bool endOfOptions = false;
    QStringList targets;
    for (++i; i < tokens.size(); ++i) {
        const QString &t = tokens.at(i);
        if (!endOfOptions && t == QLatin1String("--")) { endOfOptions = true; continue; }
        if (!endOfOptions && t.startsWith(QLatin1String("--"))) {
            if (t == QLatin1String("--recursive")) recursive = true;
            continue;
        }
        if (!endOfOptions && t.startsWith(QLatin1Char('-')) && t.size() > 1) {
            if (t.contains(QLatin1Char('r')) || t.contains(QLatin1Char('R'))) recursive = true;
            continue;
        }
        targets << t;
    }
    if (!recursive) return false;
    for (const QString &t : targets) {
        if (isDangerousRmTarget(t)) return true;
    }
    return false;
}

} // namespace

bool isDangerous(const QString &command) {
    // Her basit komutu ayrı değerlendir: "cd ~ && rm -rf ." gibi zincirler dahil.
    static const QRegularExpression splitter(QStringLiteral("&&|\\|\\||;|\\||\\n"));
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &part : command.split(splitter, Qt::SkipEmptyParts)) {
        QStringList tokens = stripPrivilege(part).split(ws, Qt::SkipEmptyParts);
        if (isDangerousRm(tokens)) return true;
    }

    const QString s = stripPrivilege(command);
    static const QString end = QStringLiteral("(?=\\s|$|;|&|\\|)");
    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("(^|[;&|\\s])mkfs(?:\\.\\w+)?\\s")),
        QRegularExpression(QStringLiteral("\\bdd\\b.*\\bof=/dev/(?!null\\b|zero\\b|stdout\\b|stderr\\b|tty\\b)")),
        QRegularExpression(QStringLiteral(">\\s*/dev/(?:sd|nvme|hd|vd|mmcblk)")),
        QRegularExpression(QStringLiteral(":\\(\\)\\s*\\{")),
        QRegularExpression(QStringLiteral("\\bch(?:mod|own)\\b[^;&|\\n]*\\s-[a-zA-Z]*R[a-zA-Z]*\\b[^;&|\\n]*\\s/") + end),
    };
    for (const auto &re : patterns) {
        if (re.match(s).hasMatch()) return true;
    }
    return false;
}

bool needsTerminal(const QString &command) {
    const QString t = command.trimmed();
    if (privilegePrefix().match(t).hasMatch()) return true;
    static const QStringList interactive = {
        QStringLiteral("htop"), QStringLiteral("btop"), QStringLiteral("top"), QStringLiteral("vim"),
        QStringLiteral("nvim"), QStringLiteral("vi"), QStringLiteral("nano"), QStringLiteral("less"),
        QStringLiteral("more"), QStringLiteral("man"), QStringLiteral("ssh"), QStringLiteral("watch"),
        QStringLiteral("tmux"), QStringLiteral("screen"), QStringLiteral("mc"), QStringLiteral("ranger"),
        QStringLiteral("nmtui")};
    const QString tok = firstToken(t);
    if (interactive.contains(tok)) return true;
    if (tok == QLatin1String("journalctl")) {
        static const QRegularExpression follow(QStringLiteral("\\s(-f|--follow)(\\s|$)"));
        return follow.match(t).hasMatch();
    }
    return false;
}

QStringList terminalArgs(const QString &command) {
    // Komut bittikten sonra çıkış kodunu göster ve kullanıcının kabuğunda aç kal.
    // eval "$1": ayrıştırma hatasında (kapanmamış tırnak vb.) bile terminal açık kalır
    // ve hata görünür; komut yine tek argv öğesidir ("ravenspot" = $0).
    return {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
            QStringLiteral("eval \"$1\"; printf \"\\n[exit %s] \" $?; exec \"${SHELL:-bash}\" -i"),
            QStringLiteral("ravenspot"), command};
}

QStringList inlineArgs(const QString &command) {
    return {QStringLiteral("-c"), command};
}

QString firstLine(const QString &command) {
    const QStringList lines = command.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty()) return l.trimmed();
    }
    return QString();
}

int extraLineCount(const QString &command) {
    int nonEmpty = 0;
    for (const QString &l : command.split(QLatin1Char('\n'))) {
        if (!l.trimmed().isEmpty()) ++nonEmpty;
    }
    return std::max(0, nonEmpty - 1);
}

QVariantMap analyze(const QString &text, const ExecutableCheck &isExecutable) {
    return {
        {QStringLiteral("signal"), static_cast<int>(detect(text, isExecutable))},
        {QStringLiteral("dangerous"), isDangerous(text)},
        {QStringLiteral("needsTerminal"), needsTerminal(text)},
        {QStringLiteral("firstLine"), firstLine(text)},
        {QStringLiteral("extraLines"), extraLineCount(text)},
    };
}

} // namespace ravenspot::cmd
