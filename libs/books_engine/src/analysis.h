// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FinancialAnalysis: the dashboard numbers — key ratios, the expense mix,
// monthly trends and a simple forecast (docs/books.md "Financial
// analysis"). Every figure carries a one-sentence definition for the UI's
// tooltip, so what the number means travels with it.
//
// Ratios are exact fixed-point integers (value with `scale` decimals),
// never floating point: a ratio is computed from integer minor-unit
// totals with one half-up rounding.

#include "books_types.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class RatioUnit { Percent, Times, Days };

struct Ratio {
    QString id;         // "gross_margin", "net_margin", "current_ratio", "quick_ratio",
                        // "debt_to_equity", "dso", "dpo"
    QString title;
    QString definition; // tooltip text
    RatioUnit unit = RatioUnit::Percent;
    qint64 numerator = 0;   // minor units (days: × period days)
    qint64 denominator = 0; // minor units
    bool available = false; // false when the denominator is zero
    qint64 value = 0;       // fixed-point with `scale` decimals
    int scale = 1;
    [[nodiscard]] QString display() const; // "42.5%", "1.83×", "37 days", "—"
};

struct ExpenseShare {
    qint64 accountId = 0; // 0 = "Everything else"
    QString name;
    Money amount;
    qint64 share = 0;     // percent with 1 decimal (425 = 42.5%)
    QString definition;
};

struct TrendPoint {
    QDate month; // first day of the month
    Money revenue;
    Money expenses;
    Money netIncome;
    Money cashBalance; // at month end
    bool forecast = false;
};

class FinancialAnalysis {
public:
    explicit FinancialAnalysis(CompanyFile &file);

    // Ratios over the period [from, to]; balance-sheet ratios as of `to`.
    [[nodiscard]] QList<Ratio> ratios(const QDate &from, const QDate &to,
                                      std::optional<Basis> basis = {}) const;
    // The largest `top` expense accounts plus "Everything else".
    [[nodiscard]] QList<ExpenseShare> expenseBreakdown(const QDate &from, const QDate &to, int top = 8,
                                                       std::optional<Basis> basis = {}) const;
    // `months` calendar months ending with the month containing `lastMonth`.
    [[nodiscard]] QList<TrendPoint> monthlyTrends(const QDate &lastMonth, int months,
                                                  std::optional<Basis> basis = {}) const;
    // Trailing average of the last `window` months of `history`, projected
    // flat for `monthsAhead` months; cash carries forward by the projected
    // net income. Points are marked forecast = true.
    [[nodiscard]] QList<TrendPoint> forecast(const QList<TrendPoint> &history, int monthsAhead,
                                             int window = 3) const;

    [[nodiscard]] static QString definition(const QString &metricId);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
