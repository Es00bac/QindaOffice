// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_time.h"

#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>

namespace QindaOffice::Plan {

namespace {
const QDate kEpoch(1970, 1, 1);

Minutes floorDiv(Minutes a, Minutes b)
{
    Minutes q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}
} // namespace

Minutes toMinutes(const QDate &date, int minuteOfDay)
{
    if (!date.isValid()) {
        return kNoDate;
    }
    return kEpoch.daysTo(date) * 1440 + minuteOfDay;
}

Minutes toMinutes(const QDateTime &wallClock)
{
    if (!wallClock.isValid()) {
        return kNoDate;
    }
    const QTime t = wallClock.time();
    return toMinutes(wallClock.date(), t.hour() * 60 + t.minute());
}

QDate dateOf(Minutes instant)
{
    if (instant == kNoDate) {
        return {};
    }
    return kEpoch.addDays(floorDiv(instant, 1440));
}

int minuteOfDay(Minutes instant)
{
    if (instant == kNoDate) {
        return 0;
    }
    return int(instant - floorDiv(instant, 1440) * 1440);
}

QDateTime toDateTime(Minutes instant)
{
    if (instant == kNoDate) {
        return {};
    }
    const int m = minuteOfDay(instant);
    return QDateTime(dateOf(instant), QTime(m / 60, m % 60), QTimeZone::UTC);
}

QString toIso(Minutes instant)
{
    if (instant == kNoDate) {
        return {};
    }
    return toDateTime(instant).toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
}

Minutes fromIso(const QString &text, int defaultMinute)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return kNoDate;
    }
    if (t.size() == 10) {
        const QDate d = QDate::fromString(t, Qt::ISODate);
        return d.isValid() ? toMinutes(d, defaultMinute) : kNoDate;
    }
    QString normalized = t;
    normalized.replace(QLatin1Char(' '), QLatin1Char('T'));
    for (const char *format : {"yyyy-MM-ddTHH:mm:ss", "yyyy-MM-ddTHH:mm"}) {
        const QDateTime dt = QDateTime::fromString(normalized.left(QByteArray(format).size()),
                                                   QString::fromLatin1(format));
        if (dt.isValid()) {
            return toMinutes(dt);
        }
    }
    return kNoDate;
}

QString formatDate(Minutes instant, const QLocale &locale)
{
    if (instant == kNoDate) {
        return {};
    }
    return locale.toString(dateOf(instant), QStringLiteral("ddd d MMM yy"));
}

QString formatDateTime(Minutes instant, const QLocale &locale)
{
    if (instant == kNoDate) {
        return {};
    }
    const int m = minuteOfDay(instant);
    return formatDate(instant, locale) + QLatin1Char(' ') + locale.toString(QTime(m / 60, m % 60), QStringLiteral("HH:mm"));
}

std::optional<Minutes> parseDate(const QString &text, int defaultMinute, const QLocale &locale)
{
    QString t = text.trimmed();
    if (t.isEmpty()) {
        return std::nullopt;
    }
    const Minutes iso = fromIso(t, defaultMinute);
    if (iso != kNoDate) {
        return iso;
    }
    // An optional trailing "HH:mm".
    int minute = defaultMinute;
    static const QRegularExpression timeRe(QStringLiteral("\\s+(\\d{1,2}):(\\d{2})$"));
    const QRegularExpressionMatch tm = timeRe.match(t);
    if (tm.hasMatch()) {
        const int h = tm.captured(1).toInt();
        const int mi = tm.captured(2).toInt();
        if (h > 24 || mi > 59 || (h == 24 && mi != 0)) {
            return std::nullopt;
        }
        minute = h * 60 + mi;
        t = t.left(tm.capturedStart()).trimmed();
    }
    const QStringList formats{QStringLiteral("ddd d MMM yy"), QStringLiteral("d MMM yy"), QStringLiteral("ddd d MMM yyyy"),
                              QStringLiteral("d MMM yyyy"),   QStringLiteral("M/d/yyyy"), QStringLiteral("M/d/yy"),
                              QStringLiteral("d.M.yyyy"),     QStringLiteral("d.M.yy")};
    for (const QString &format : formats) {
        QDate d = locale.toDate(t, format);
        if (!d.isValid() && locale != QLocale::c()) {
            d = QLocale::c().toDate(t, format);
        }
        if (d.isValid()) {
            if (format.endsWith(QLatin1String(" yy")) || format.endsWith(QLatin1String("/yy"))
                || format.endsWith(QLatin1String(".yy"))) {
                // Two-digit years read as 1900-based by Qt; projects live now.
                if (d.year() < 1970) {
                    d = d.addYears(100);
                }
            }
            return toMinutes(d, minute);
        }
    }
    return std::nullopt;
}

DurationFormat durationFormatOf(const ProjectInfo &info)
{
    return {info.minutesPerDay, info.minutesPerWeek, info.daysPerMonth};
}

Minutes minutesPerUnit(DurationUnit unit, const DurationFormat &format)
{
    switch (unit) {
    case DurationUnit::Minutes:
        return 1;
    case DurationUnit::Hours:
        return 60;
    case DurationUnit::Days:
        return format.minutesPerDay;
    case DurationUnit::Weeks:
        return format.minutesPerWeek;
    case DurationUnit::Months:
        return Minutes(format.minutesPerDay) * format.daysPerMonth;
    }
    return format.minutesPerDay;
}

std::optional<ParsedDuration> parseDuration(const QString &text, const DurationFormat &format, bool allowNegative)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([+-]?)\\s*(\\d+(?:[.,]\\d+)?|[.,]\\d+)\\s*([a-zA-Z]*)\\s*(\\?)?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const bool negative = m.captured(1) == QLatin1String("-");
    if (negative && !allowNegative) {
        return std::nullopt;
    }
    QString number = m.captured(2);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = number.toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    const QString u = m.captured(3).toLower();
    DurationUnit unit = DurationUnit::Days;
    if (u.isEmpty() || u == QLatin1String("d") || u == QLatin1String("day") || u == QLatin1String("days")
        || u == QLatin1String("dy") || u == QLatin1String("dys")) {
        unit = DurationUnit::Days;
    } else if (u == QLatin1String("h") || u == QLatin1String("hr") || u == QLatin1String("hrs")
               || u == QLatin1String("hour") || u == QLatin1String("hours")) {
        unit = DurationUnit::Hours;
    } else if (u == QLatin1String("m") || u == QLatin1String("min") || u == QLatin1String("mins")
               || u == QLatin1String("minute") || u == QLatin1String("minutes")) {
        unit = DurationUnit::Minutes;
    } else if (u == QLatin1String("w") || u == QLatin1String("wk") || u == QLatin1String("wks")
               || u == QLatin1String("week") || u == QLatin1String("weeks")) {
        unit = DurationUnit::Weeks;
    } else if (u == QLatin1String("mo") || u == QLatin1String("mon") || u == QLatin1String("mons")
               || u == QLatin1String("month") || u == QLatin1String("months")) {
        unit = DurationUnit::Months;
    } else {
        return std::nullopt;
    }
    const double minutes = value * double(minutesPerUnit(unit, format));
    if (minutes > 1e12) {
        return std::nullopt;
    }
    ParsedDuration out;
    out.minutes = Minutes(std::llround(minutes)) * (negative ? -1 : 1);
    out.unit = unit;
    out.estimated = !m.captured(4).isEmpty();
    return out;
}

namespace {
QString number(double value, const QLocale &locale)
{
    // Up to two decimals, trailing zeros dropped: 1.5, 2, 0.25.
    QString s = locale.toString(value, 'f', 2);
    const QChar dot = locale.decimalPoint().at(0);
    if (s.contains(dot)) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(dot)) {
            s.chop(1);
        }
    }
    return s;
}

QString unitWord(DurationUnit unit, bool one)
{
    switch (unit) {
    case DurationUnit::Minutes:
        return one ? QStringLiteral("min") : QStringLiteral("mins");
    case DurationUnit::Hours:
        return one ? QStringLiteral("hr") : QStringLiteral("hrs");
    case DurationUnit::Days:
        return one ? QStringLiteral("day") : QStringLiteral("days");
    case DurationUnit::Weeks:
        return one ? QStringLiteral("wk") : QStringLiteral("wks");
    case DurationUnit::Months:
        return one ? QStringLiteral("mon") : QStringLiteral("mons");
    }
    return QStringLiteral("days");
}
} // namespace

QString formatDuration(Minutes minutes, DurationUnit unit, const DurationFormat &format, bool estimated,
                       const QLocale &locale)
{
    const double value = double(minutes) / double(minutesPerUnit(unit, format));
    const bool one = std::abs(value - 1.0) < 1e-9 || std::abs(value + 1.0) < 1e-9;
    return number(value, locale) + QLatin1Char(' ') + unitWord(unit, one) + (estimated ? QStringLiteral("?") : QString());
}

QString formatSpan(Minutes minutes, const DurationFormat &format, const QLocale &locale)
{
    const DurationUnit unit = minutes % 60 == 0 && (minutes % format.minutesPerDay == 0 || std::abs(minutes) >= format.minutesPerDay)
                                  ? DurationUnit::Days
                                  : DurationUnit::Hours;
    return formatDuration(minutes, unit, format, false, locale);
}

QString formatWork(Minutes minutes, const QLocale &locale)
{
    return formatDuration(minutes, DurationUnit::Hours, DurationFormat{}, false, locale);
}

QString formatLag(const Link &link, const DurationFormat &format, const QLocale &locale)
{
    if (link.lagPercent) {
        const int p = *link.lagPercent;
        if (p == 0) {
            return {};
        }
        return (p > 0 ? QStringLiteral("+") : QString()) + locale.toString(p) + QLatin1Char('%');
    }
    if (link.lag == 0) {
        return {};
    }
    const DurationUnit unit = link.lag % format.minutesPerDay == 0 ? DurationUnit::Days : DurationUnit::Hours;
    return (link.lag > 0 ? QStringLiteral("+") : QString()) + formatDuration(link.lag, unit, format, false, locale);
}

int currencyDecimals(const QString &currency)
{
    static const QStringList zero{QStringLiteral("JPY"), QStringLiteral("KRW"), QStringLiteral("VND"),
                                  QStringLiteral("CLP"), QStringLiteral("ISK"), QStringLiteral("HUF")};
    return zero.contains(currency.toUpper()) ? 0 : 2;
}

qint64 divRound(qint64 a, qint64 b)
{
    if (b <= 0) {
        return 0;
    }
    const qint64 half = b / 2;
    return a >= 0 ? (a + half) / b : -((-a + half) / b);
}

QString formatMoney(qint64 minor, const QString &currency, const QLocale &locale)
{
    const int decimals = currencyDecimals(currency);
    qint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    const qint64 whole = std::abs(minor) / scale;
    const qint64 frac = std::abs(minor) % scale;
    QLocale grouped = locale; // the C locale omits group separators by default
    grouped.setNumberOptions(locale.numberOptions() & ~QLocale::OmitGroupSeparator);
    QString text = grouped.toString(whole);
    if (decimals > 0) {
        text += locale.decimalPoint() + QStringLiteral("%1").arg(frac, decimals, 10, QLatin1Char('0'));
    }
    const QString code = currency.toUpper();
    QString symbol = code + QLatin1Char(' ');
    if (code == QLatin1String("USD") || code == QLatin1String("CAD") || code == QLatin1String("AUD")) {
        symbol = QStringLiteral("$");
    } else if (code == QLatin1String("EUR")) {
        symbol = QStringLiteral("€");
    } else if (code == QLatin1String("GBP")) {
        symbol = QStringLiteral("£");
    }
    return (minor < 0 ? QStringLiteral("-") : QString()) + symbol + text;
}

std::optional<qint64> parseMoney(const QString &text, const QString &currency, const QLocale &locale)
{
    QString t = text.trimmed();
    t.remove(QLatin1Char('$'));
    t.remove(currency, Qt::CaseInsensitive);
    t.remove(locale.groupSeparator());
    t.remove(QLatin1Char(' '));
    if (locale.decimalPoint() != QLatin1String(".")) {
        t.replace(locale.decimalPoint(), QStringLiteral("."));
    }
    static const QRegularExpression re(QStringLiteral("^(-?)(\\d*)(?:\\.(\\d*))?$"));
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch() || (m.captured(2).isEmpty() && m.captured(3).isEmpty())) {
        return std::nullopt;
    }
    const int decimals = currencyDecimals(currency);
    QString frac = m.captured(3);
    if (frac.size() > decimals) {
        return std::nullopt;
    }
    frac = frac.leftJustified(decimals, QLatin1Char('0'));
    bool ok = true;
    const qint64 whole = m.captured(2).isEmpty() ? 0 : m.captured(2).toLongLong(&ok);
    if (!ok || whole > 1'000'000'000'000LL) {
        return std::nullopt;
    }
    qint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    const qint64 value = whole * scale + (frac.isEmpty() ? 0 : frac.toLongLong());
    return m.captured(1).isEmpty() ? value : -value;
}

} // namespace QindaOffice::Plan
