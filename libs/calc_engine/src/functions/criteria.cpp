// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's criteria as COUNTIF/SUMIF/…IFS and the database functions read
// them: an optional comparison operator, then a number, a text with * ? ~
// wildcards, or an error's spelling. Split from functions.cpp (M69).
#include "functions.h"

#include <QRegularExpression>

#include <functional>

namespace QindaOffice::Calc {

namespace {

// Builds a case-insensitive anchored regex from Excel's * ? wildcards,
// escaping every other regex metacharacter and honouring the ~ escape
// (~* and ~? are literal; ~~ is a literal tilde).
QRegularExpression wildcardRegex(const QString &criteria)
{
    QString pattern;
    for (int i = 0; i < criteria.size(); ++i) {
        const QChar c = criteria.at(i);
        if (c == QLatin1Char('~') && i + 1 < criteria.size()) {
            const QChar next = criteria.at(++i);
            pattern += QRegularExpression::escape(QString(next));
            continue;
        }
        if (c == QLatin1Char('*')) {
            pattern += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            pattern += QLatin1Char('.');
        } else {
            pattern += QRegularExpression::escape(QString(c));
        }
    }
    return QRegularExpression(QStringLiteral("^") + pattern + QStringLiteral("$"),
                              QRegularExpression::CaseInsensitiveOption);
}

bool textMatchesWithWildcards(const QString &text, const QString &criteria)
{
    if (criteria.contains(QLatin1Char('*')) || criteria.contains(QLatin1Char('?'))
        || criteria.contains(QLatin1Char('~'))) {
        return wildcardRegex(criteria).match(text).hasMatch();
    }
    return QString::compare(text, criteria, Qt::CaseInsensitive) == 0;
}

} // namespace

bool criteriaMatches(const Value &cell, const QString &rawCriteria)
{
    const QString criteria = rawCriteria.trimmed();
    if (criteria.isEmpty()) {
        return isBlank(cell);
    }
    struct Op {
        QString prefix;
        std::function<bool(int cmp)> check;
    };
    static const Op kOps[] = {
        {QStringLiteral(">="), [](int c) { return c >= 0; }},
        {QStringLiteral("<="), [](int c) { return c <= 0; }},
        {QStringLiteral("<>"), [](int c) { return c != 0; }},
        {QStringLiteral(">"), [](int c) { return c > 0; }},
        {QStringLiteral("<"), [](int c) { return c < 0; }},
        {QStringLiteral("="), [](int c) { return c == 0; }},
    };
    for (const Op &op : kOps) {
        if (criteria.startsWith(op.prefix)) {
            const QString rest = criteria.mid(op.prefix.size());
            bool numOk = false;
            const double num = rest.toDouble(&numOk);
            if (numOk) {
                // A blank is not a number: it fails every numeric comparison
                // except "not equal" (Excel's COUNTIF(range,"<1") skips
                // blanks; the old coercion counted them as 0 — M61).
                if (isBlank(cell)) {
                    return op.prefix == QLatin1String("<>");
                }
                const std::optional<double> cellNum = toNumber(cell);
                if (!cellNum.has_value()) {
                    // Excel: "<>5" counts TEXT cells as not-equal; only
                    // blanks fall outside every numeric comparison.
                    return op.prefix == QLatin1String("<>") && !isBlank(cell);
                }
                const double diff = *cellNum - num;
                return op.check(diff == 0 ? 0 : (diff < 0 ? -1 : 1));
            }
            // Wildcards apply after operators too ("=a*", "<>b?").
            if (rest.contains(QLatin1Char('*')) || rest.contains(QLatin1Char('?'))
                || rest.contains(QLatin1Char('~'))) {
                const bool matched = textMatchesWithWildcards(toText(cell), rest);
                return op.check(matched ? 0 : 1);
            }
            const int cmp = QString::compare(toText(cell), rest, Qt::CaseInsensitive);
            return op.check(cmp == 0 ? 0 : (cmp < 0 ? -1 : 1));
        }
    }
    // Wildcard / equality match: COUNTIF/SUMIF equality is
    // case-insensitive (matches Excel and the LibreOffice oracle).
    const Value criterionValue = [&criteria]() -> Value {
        bool numOk = false;
        const double num = criteria.toDouble(&numOk);
        if (numOk) {
            return num;
        }
        return criteria;
    }();
    if (criteria.contains(QLatin1Char('*')) || criteria.contains(QLatin1Char('?'))
        || criteria.contains(QLatin1Char('~'))) {
        return textMatchesWithWildcards(toText(cell), criteria);
    }
    // An error cell matches its own spelling ("#N/A"), as in Excel (M61).
    if (const auto *err = std::get_if<Err>(&cell)) {
        return QString::compare(errText(*err), criteria, Qt::CaseInsensitive) == 0;
    }
    // A number stored as text matches a numeric criterion ("5" matches 5,
    // as Excel's COUNTIF does); text against text ignores case.
    // AGENT-GUARD: read criterionValue only as the alternative it holds —
    // before M69 a text cell "5" against a text criterion took
    // std::get<double> of the text and threw out of the recalculation.
    if (isText(cell)) {
        const QString cellText = std::get<QString>(cell);
        if (isNumber(criterionValue)) {
            bool cellNumOk = false;
            const double cellNum = cellText.toDouble(&cellNumOk);
            return cellNumOk && qFuzzyCompare(cellNum, std::get<double>(criterionValue));
        }
        return QString::compare(cellText, std::get<QString>(criterionValue), Qt::CaseInsensitive) == 0;
    }
    return compareValues(cell, criterionValue) == 0;
}

} // namespace QindaOffice::Calc
