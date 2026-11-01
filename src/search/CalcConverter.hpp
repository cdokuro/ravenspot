#pragma once

#include "SearchResultItem.hpp"
#include <QString>
#include <optional>

namespace ravenspot {

class CalcConverter {
public:
    CalcConverter() = default;
    ~CalcConverter() = default;

    // Evaluates query; returns SearchResultItem if query is valid math or unit conversion
    [[nodiscard]] std::optional<SearchResultItem> evaluate(const QString &query) const;

    // Direct string evaluation methods for test harness parity
    [[nodiscard]] static std::optional<QString> evaluateMathExpression(const QString &expr);
    [[nodiscard]] static std::optional<QString> evaluateUnitConversion(const QString &query);

private:
    static std::optional<SearchResultItem> tryUnitConversion(const QString &query);
    static std::optional<SearchResultItem> tryMathEvaluation(const QString &query);
};

} // namespace ravenspot
