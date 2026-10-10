// SPDX-License-Identifier: GPL-3.0-or-later
// Exact money and plain numbers as text (declared in base_types.h; split
// from base_types.cpp in M86 to keep each file within the size rule).
//
// AGENT-NOTE (D-049 §2): currency never passes through floating point —
// parseMoney reads the decimal text digit by digit into minor units and
// formatMoney prints minor units with integer arithmetic.
#include "base_types.h"

#include <QRegularExpression>

#include <cmath>

namespace QindaOffice::Base {

namespace {

QString withoutGrouping(QString text)
{
    text.remove(QLatin1Char(','));
    text.remove(QLatin1Char(' '));
    text.remove(QChar(0x00A0));
    text.remove(QChar(0x202F));
    return text;
}

} // namespace

std::optional<qint64> parseMoney(const QString &raw, int decimals, QString *error)
{
    QString text = raw.trimmed();
    bool negative = false;
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        negative = true;
        text = text.mid(1, text.size() - 2).trimmed();
    }
    // Drop any currency symbol or code around the digits.
    static const QRegularExpression symbols(QStringLiteral("[^0-9.,\\-+\\s]"));
    text.remove(symbols);
    text = withoutGrouping(text);
    if (text.startsWith(QLatin1Char('-'))) {
        negative = !negative;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('+'))) {
        text.remove(0, 1);
    }
    static const QRegularExpression shape(QStringLiteral("^(\\d*)(?:\\.(\\d*))?$"));
    const QRegularExpressionMatch m = shape.match(text);
    if (text.isEmpty() || !m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty())) {
        if (error != nullptr) {
            *error = QStringLiteral("needs an amount");
        }
        return std::nullopt;
    }
    const QString whole = m.captured(1);
    QString fraction = m.captured(2);
    while (fraction.size() > decimals && fraction.endsWith(QLatin1Char('0'))) {
        fraction.chop(1);
    }
    if (fraction.size() > decimals) {
        if (error != nullptr) {
            *error = QStringLiteral("takes at most %1 decimal places").arg(decimals);
        }
        return std::nullopt;
    }
    fraction = fraction.leftJustified(decimals, QLatin1Char('0'));
    const QString digits = (whole.isEmpty() ? QStringLiteral("0") : whole) + fraction;
    if (digits.size() > 18) {
        if (error != nullptr) {
            *error = QStringLiteral("is too large");
        }
        return std::nullopt;
    }
    bool ok = false;
    const qint64 minor = digits.toLongLong(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = QStringLiteral("needs an amount");
        }
        return std::nullopt;
    }
    return negative ? -minor : minor;
}

QString formatMoney(qint64 minor, int decimals, const QString &symbol, bool grouping)
{
    const bool negative = minor < 0;
    // AGENT-GUARD: unsigned magnitude so INT64_MIN does not overflow.
    const quint64 magnitude = negative ? quint64(0) - quint64(minor) : quint64(minor);
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    QString whole = QString::number(magnitude / scale);
    if (grouping) {
        for (int i = int(whole.size()) - 3; i > 0; i -= 3) {
            whole.insert(i, QLatin1Char(','));
        }
    }
    QString out = symbol + whole;
    if (decimals > 0) {
        out += QLatin1Char('.') + QString::number(magnitude % scale).rightJustified(decimals, QLatin1Char('0'));
    }
    return negative ? QLatin1Char('-') + out : out;
}

QString formatNumber(double value)
{
    if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 1e15) {
        return QString::number(qint64(value));
    }
    return QString::number(value, 'g', 15);
}

} // namespace QindaOffice::Base
