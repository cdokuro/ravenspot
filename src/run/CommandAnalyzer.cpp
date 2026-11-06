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
    if (t.startsWith(QLatin1String("./")) || t.startsWith(QLatin1String("~/")) || t.startsWith(QLatin1Char('/')))
        return Signal::Strong;
    if (shellSyntax().match(t).hasMatch()) return Signal::Strong;

    const QStringList tokens = t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    // Tek kelime asla kendiliğinden komut sayılmaz ("firefox" uygulama araması kalır).
    if (tokens.size() >= 2 && isExecutable && isExecutable(tokens.first())) return Signal::Weak;
    return Signal::None;
}

bool isDangerous(const QString &command) {
    const QString s = stripPrivilege(command);
    // Hedef tam olarak kök, kök/*, ev dizini (~, ~/, $HOME) ise; alt yollar değil.
    static const QString end = QStringLiteral("(?=\\s|$|;|&|\\|)");
    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("\\brm\\s+(?:-\\S+\\s+)*-[a-zA-Z]*[rR][a-zA-Z]*\\s+(?:-\\S+\\s+)*"
                                          "(?:/\\*?|~/?|\"?\\$HOME/?\"?)") + end),
        QRegularExpression(QStringLiteral("\\bmkfs(?:\\.\\w+)?\\b")),
        QRegularExpression(QStringLiteral("\\bdd\\b.*\\bof=/dev/")),
        QRegularExpression(QStringLiteral(">\\s*/dev/(?:sd|nvme|hd|vd|mmcblk)")),
        QRegularExpression(QStringLiteral(":\\(\\)\\s*\\{")),
        QRegularExpression(QStringLiteral("\\bch(?:mod|own)\\b.*\\s-[a-zA-Z]*R[a-zA-Z]*\\b.*\\s/") + end),
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
    return {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
            command + QStringLiteral("\nprintf \"\\n[exit %s] \" $?; exec \"${SHELL:-bash}\" -i")};
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
