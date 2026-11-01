#include "CalcConverter.hpp"

#include <QJSEngine>
#include <QJSValue>
#include <QRegularExpression>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ravenspot {

std::optional<SearchResultItem> CalcConverter::evaluate(const QString &query) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return std::nullopt;

    // First try unit conversion
    if (auto unitRes = tryUnitConversion(trimmed)) {
        return unitRes;
    }

    // Next try math evaluation
    if (auto mathRes = tryMathEvaluation(trimmed)) {
        return mathRes;
    }

    return std::nullopt;
}

std::optional<QString> CalcConverter::evaluateMathExpression(const QString &expr) {
    QString sanitized = expr.trimmed();
    if (sanitized.isEmpty()) return std::nullopt;

    // Must contain at least one math operator or function
    static const QRegularExpression mathPattern(
        QStringLiteral(R"(^[\d\s\.\+\-\*\/\^\(\)\%\,\e\E\pi\s]+$)")
    );

    // Filter out plain numbers without any operator or expression
    bool isPureNumber = false;
    sanitized.toDouble(&isPureNumber);
    if (isPureNumber && !sanitized.contains(QRegularExpression(QStringLiteral(R"([\+\-\*\/\^\(\)])")))) {
        return std::nullopt;
    }

    // Replace '^' with '**' for JS engine
    QString jsExpr = sanitized;
    jsExpr.replace(QStringLiteral("^"), QStringLiteral("**"));
    jsExpr.replace(QStringLiteral("pi"), QStringLiteral("Math.PI"), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("sqrt("), QStringLiteral("Math.sqrt("), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("sin("), QStringLiteral("Math.sin("), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("cos("), QStringLiteral("Math.cos("), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("tan("), QStringLiteral("Math.tan("), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("abs("), QStringLiteral("Math.abs("), Qt::CaseInsensitive);
    jsExpr.replace(QStringLiteral("log("), QStringLiteral("Math.log("), Qt::CaseInsensitive);

    QJSEngine engine;
    QJSValue result = engine.evaluate(jsExpr);
    if (result.isError() || !result.isNumber()) {
        return std::nullopt;
    }

    double num = result.toNumber();
    if (std::isnan(num) || std::isinf(num)) {
        return std::nullopt;
    }

    // Format clean number without redundant zeros
    std::ostringstream oss;
    oss << std::setprecision(10) << num;
    return QString::fromStdString(oss.str());
}

std::optional<QString> CalcConverter::evaluateUnitConversion(const QString &query) {
    static const QRegularExpression unitRegex(
        QStringLiteral(R"(^\s*([\d\.\-]+)\s*([a-zA-Z°]+)\s+(?:to|in|=|->)\s+([a-zA-Z°]+)\s*$)"),
        QRegularExpression::CaseInsensitiveOption
    );

    auto match = unitRegex.match(query);
    if (!match.hasMatch()) return std::nullopt;

    bool ok = false;
    double val = match.captured(1).toDouble(&ok);
    if (!ok) return std::nullopt;

    QString from = match.captured(2).toLower();
    QString to = match.captured(3).toLower();

    // Distance
    if ((from == "km" || from == "kilometer" || from == "kilometers") &&
        (to == "mi" || to == "mile" || to == "miles")) {
        double res = val * 0.621371;
        return QStringLiteral("%1 miles").arg(QString::number(res, 'f', 2));
    }
    if ((from == "mi" || from == "mile" || from == "miles") &&
        (to == "km" || to == "kilometer" || to == "kilometers")) {
        double res = val * 1.60934;
        return QStringLiteral("%1 km").arg(QString::number(res, 'f', 2));
    }
    if ((from == "m" || from == "meter" || from == "meters") &&
        (to == "ft" || to == "feet" || to == "foot")) {
        double res = val * 3.28084;
        return QStringLiteral("%1 ft").arg(QString::number(res, 'f', 2));
    }
    if ((from == "ft" || from == "feet" || from == "foot") &&
        (to == "m" || to == "meter" || to == "meters")) {
        double res = val * 0.3048;
        return QStringLiteral("%1 m").arg(QString::number(res, 'f', 2));
    }
    if ((from == "cm" || from == "centimeter") &&
        (to == "in" || to == "inch" || to == "inches")) {
        double res = val * 0.393701;
        return QStringLiteral("%1 in").arg(QString::number(res, 'f', 2));
    }
    if ((from == "in" || from == "inch" || from == "inches") &&
        (to == "cm" || to == "centimeter")) {
        double res = val * 2.54;
        return QStringLiteral("%1 cm").arg(QString::number(res, 'f', 2));
    }

    // Weight
    if ((from == "kg" || from == "kilo" || from == "kilograms") &&
        (to == "lbs" || to == "lb" || to == "pounds")) {
        double res = val * 2.20462;
        return QStringLiteral("%1 lbs").arg(QString::number(res, 'f', 2));
    }
    if ((from == "lbs" || from == "lb" || from == "pounds") &&
        (to == "kg" || to == "kilo" || to == "kilograms")) {
        double res = val * 0.453592;
        return QStringLiteral("%1 kg").arg(QString::number(res, 'f', 2));
    }
    if ((from == "g" || from == "gram" || from == "grams") &&
        (to == "oz" || to == "ounces")) {
        double res = val * 0.035274;
        return QStringLiteral("%1 oz").arg(QString::number(res, 'f', 2));
    }

    // Temperature
    if ((from == "c" || from == "celsius" || from == "°c") &&
        (to == "f" || to == "fahrenheit" || to == "°f")) {
        double res = (val * 9.0 / 5.0) + 32.0;
        return QStringLiteral("%1 °F").arg(QString::number(res, 'f', 1));
    }
    if ((from == "f" || from == "fahrenheit" || from == "°f") &&
        (to == "c" || to == "celsius" || to == "°c")) {
        double res = (val - 32.0) * 5.0 / 9.0;
        return QStringLiteral("%1 °C").arg(QString::number(res, 'f', 1));
    }

    return std::nullopt;
}

std::optional<SearchResultItem> CalcConverter::tryUnitConversion(const QString &query) {
    auto conv = evaluateUnitConversion(query);
    if (!conv) return std::nullopt;

    SearchResultItem item;
    item.id = QStringLiteral("__unit_conversion__");
    item.name = QStringLiteral("= ") + *conv;
    item.comment = query;
    item.icon = QStringLiteral("󱉏");
    item.iconName = QStringLiteral("accessories-calculator");
    item.type = QStringLiteral("unit");
    item.exec = *conv;
    item.path = QStringLiteral("Dönüştürücü");
    item.provider = QStringLiteral("clipboard-item");
    item.value = *conv;
    item.score = -200; // Top priority
    return item;
}

std::optional<SearchResultItem> CalcConverter::tryMathEvaluation(const QString &query) {
    auto math = evaluateMathExpression(query);
    if (!math) return std::nullopt;

    SearchResultItem item;
    item.id = QStringLiteral("__math_calc__");
    item.name = QStringLiteral("= ") + *math;
    item.comment = query;
    item.icon = QStringLiteral("");
    item.iconName = QStringLiteral("accessories-calculator");
    item.type = QStringLiteral("calc");
    item.exec = *math;
    item.path = QStringLiteral("Hesap Makinesi");
    item.provider = QStringLiteral("clipboard-item");
    item.value = *math;
    item.score = -200; // Top priority
    return item;
}

} // namespace ravenspot
