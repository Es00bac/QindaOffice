// SPDX-License-Identifier: GPL-3.0-or-later
// The MSPDI wire converters (mspdi_p.h) and the format sniff (mspdi.h).
#include "mspdi.h"
#include "mspdi_p.h"

#include "plan_time.h"

#include <QRegularExpression>

namespace QindaOffice::Plan {

namespace Mspdi {

std::optional<Minutes> parseWireDuration(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(
        "^\\s*(-?)P(?:(\\d+(?:\\.\\d+)?)D)?(?:T(?:(\\d+(?:\\.\\d+)?)H)?(?:(\\d+(?:\\.\\d+)?)M)?(?:(\\d+(?:\\.\\d+)?)S)?)?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    // Everything in thousandths of a minute, then rounded once.
    qint64 milli = 0;
    const auto part = [&](int group, qint64 minutesPerUnit) {
        if (m.captured(group).isEmpty()) {
            return true;
        }
        const std::optional<qint64> v = scaledDecimal(m.captured(group), 3);
        if (!v) {
            return false;
        }
        milli += *v * minutesPerUnit;
        return true;
    };
    if (!part(2, 1440) || !part(3, 60) || !part(4, 1)) {
        return std::nullopt;
    }
    if (!m.captured(5).isEmpty()) {
        const std::optional<qint64> s = scaledDecimal(m.captured(5), 3);
        if (!s) {
            return std::nullopt;
        }
        milli += divRound(*s, 60);
    }
    const Minutes minutes = divRound(milli, 1000);
    return m.captured(1).isEmpty() ? minutes : -minutes;
}

QString wireDuration(Minutes minutes)
{
    const Minutes a = std::abs(minutes);
    return (minutes < 0 ? QStringLiteral("-") : QString())
        + QStringLiteral("PT%1H%2M0S").arg(a / 60).arg(a % 60);
}

Minutes parseWireDateTime(const QString &text)
{
    return fromIso(text.trimmed(), 0);
}

QString wireDateTime(Minutes instant)
{
    return instant == kNoDate ? QString() : toIso(instant) + QStringLiteral(":00");
}

std::optional<int> parseTimeOfDay(const QString &text, bool end)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,2}):(\\d{2})(?::(\\d{2})(?:\\.\\d+)?)?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const int h = m.captured(1).toInt();
    const int mi = m.captured(2).toInt();
    if (h > 24 || mi > 59 || (h == 24 && mi != 0)) {
        return std::nullopt;
    }
    const int minute = h * 60 + mi;
    if (end && (minute == 0 || minute == 1440)) {
        return 1440;
    }
    return minute == 1440 ? 0 : minute;
}

QString formatTimeOfDay(int minute)
{
    minute = ((minute % 1440) + 1440) % 1440;
    return QStringLiteral("%1:%2:00").arg(minute / 60, 2, 10, QLatin1Char('0')).arg(minute % 60, 2, 10, QLatin1Char('0'));
}

std::optional<qint64> scaledDecimal(const QString &text, int scale)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([+-]?)(\\d*)(?:\\.(\\d*))?(?:[eE]([+-]?\\d+))?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch() || (m.captured(2).isEmpty() && m.captured(3).isEmpty())) {
        return std::nullopt;
    }
    // Digits as one string with the decimal point moved by the exponent.
    QString digits = m.captured(2) + m.captured(3);
    int point = int(m.captured(2).size()) + (m.captured(4).isEmpty() ? 0 : m.captured(4).toInt()) + scale;
    if (point > 30 || point < -30) {
        return point < 0 ? std::optional<qint64>(0) : std::nullopt;
    }
    while (point > digits.size()) {
        digits.append(QLatin1Char('0'));
    }
    QString whole = point > 0 ? digits.left(point) : QString();
    const QChar next = point >= 0 && point < digits.size() ? digits.at(point) : QLatin1Char('0');
    whole.remove(QRegularExpression(QStringLiteral("^0+")));
    if (whole.size() > 17) {
        return std::nullopt;
    }
    qint64 value = whole.isEmpty() ? 0 : whole.toLongLong();
    if (next.digitValue() >= 5) {
        ++value; // half away from zero: the magnitude rounds up
    }
    return m.captured(1) == QLatin1String("-") ? -value : value;
}

QString decimalText(qint64 value, int scale)
{
    qint64 pow = 1;
    for (int i = 0; i < scale; ++i) {
        pow *= 10;
    }
    const qint64 a = std::abs(value);
    QString text = QString::number(a / pow);
    if (scale > 0) {
        text += QLatin1Char('.') + QStringLiteral("%1").arg(a % pow, scale, 10, QLatin1Char('0'));
    }
    return (value < 0 ? QStringLiteral("-") : QString()) + text;
}

qint64 centsToMinor(qint64 cents, int decimals)
{
    if (decimals >= 2) {
        qint64 factor = 1;
        for (int i = 2; i < decimals; ++i) {
            factor *= 10;
        }
        return cents * factor;
    }
    qint64 divisor = 1;
    for (int i = decimals; i < 2; ++i) {
        divisor *= 10;
    }
    return divRound(cents, divisor);
}

qint64 minorToCents(qint64 minor, int decimals)
{
    if (decimals <= 2) {
        qint64 factor = 1;
        for (int i = decimals; i < 2; ++i) {
            factor *= 10;
        }
        return minor * factor;
    }
    qint64 divisor = 1;
    for (int i = 2; i < decimals; ++i) {
        divisor *= 10;
    }
    return divRound(minor, divisor);
}

DurationCode durationCode(int code)
{
    DurationCode out;
    if (code >= 35 && code <= 53) {
        out.estimated = true;
        code -= 32;
    }
    switch (code) {
    case 3:
    case 4:
        out.unit = DurationUnit::Minutes;
        break;
    case 5:
    case 6:
        out.unit = DurationUnit::Hours;
        break;
    case 7:
    case 8:
        out.unit = DurationUnit::Days;
        break;
    case 9:
    case 10:
        out.unit = DurationUnit::Weeks;
        break;
    case 11:
    case 12:
        out.unit = DurationUnit::Months;
        break;
    case 19:
    case 20:
        out.percent = true;
        break;
    case 21:
        break; // "null": days
    default:
        out.known = false;
        break;
    }
    out.elapsed = code == 4 || code == 6 || code == 8 || code == 10 || code == 12 || code == 20;
    return out;
}

int durationFormat(DurationUnit unit, bool estimated)
{
    int code = 7;
    switch (unit) {
    case DurationUnit::Minutes:
        code = 3;
        break;
    case DurationUnit::Hours:
        code = 5;
        break;
    case DurationUnit::Days:
        code = 7;
        break;
    case DurationUnit::Weeks:
        code = 9;
        break;
    case DurationUnit::Months:
        code = 11;
        break;
    }
    return estimated ? code + 32 : code;
}

std::optional<LinkType> linkTypeFromWire(int code)
{
    switch (code) {
    case 0:
        return LinkType::FinishToFinish;
    case 1:
        return LinkType::FinishToStart;
    case 2:
        return LinkType::StartToFinish;
    case 3:
        return LinkType::StartToStart;
    default:
        return std::nullopt;
    }
}

int wireLinkType(LinkType type)
{
    switch (type) {
    case LinkType::FinishToFinish:
        return 0;
    case LinkType::FinishToStart:
        return 1;
    case LinkType::StartToFinish:
        return 2;
    case LinkType::StartToStart:
        return 3;
    }
    return 1;
}

} // namespace Mspdi

bool looksLikeMspdi(const QByteArray &head)
{
    const QByteArray window = head.left(4096);
    const int root = window.indexOf("<Project");
    return root >= 0 && window.indexOf("schemas.microsoft.com/project", root) > root;
}

} // namespace QindaOffice::Plan
