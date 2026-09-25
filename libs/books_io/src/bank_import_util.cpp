// SPDX-License-Identifier: GPL-3.0-or-later
// Exact amount and date parsing shared by the CSV, OFX and QIF readers,
// and format sniffing for parseFile().
#include "bank_import.h"

#include "decimal.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <limits>

namespace QindaOffice::Books::BankImport {

std::optional<qint64> parseAmount(const QString &input, int exponent, bool decimalComma)
{
    QString t = input.trimmed();
    if (t.isEmpty()) {
        return std::nullopt;
    }
    bool negative = false;
    if (t.startsWith(QLatin1Char('(')) && t.endsWith(QLatin1Char(')'))) {
        negative = true;
        t = t.mid(1, t.size() - 2).trimmed();
    }
    const QString upper = t.toUpper();
    if (upper.endsWith(QStringLiteral("DR"))) {
        negative = !negative;
        t.chop(2);
    } else if (upper.endsWith(QStringLiteral("CR"))) {
        t.chop(2);
    }
    t = t.trimmed();
    if (t.endsWith(QLatin1Char('-'))) {
        negative = !negative;
        t.chop(1);
    }
    QString digits;
    int minusSigns = 0;
    for (const QChar c : std::as_const(t)) {
        if (c.isDigit() && c.unicode() < 128) {
            digits += c;
        } else if (c == QLatin1Char('.') || c == QLatin1Char(',')) {
            digits += c;
        } else if (c == QLatin1Char('-') || c == QChar(0x2212)) {
            ++minusSigns;
        } else if (c == QLatin1Char('/') || c == QLatin1Char(':')) {
            return std::nullopt; // a date or a time, not an amount
        }
        // currency symbols and codes, spaces, apostrophe grouping, '+':
        // ignored
    }
    if (minusSigns > 1 || digits.isEmpty()) {
        return std::nullopt;
    }
    negative = negative != (minusSigns == 1);
    // Which separator is the decimal point?
    QChar point = QLatin1Char('.');
    const qsizetype lastDot = digits.lastIndexOf(QLatin1Char('.'));
    const qsizetype lastComma = digits.lastIndexOf(QLatin1Char(','));
    if (decimalComma) {
        point = QLatin1Char(',');
    } else if (lastDot >= 0 && lastComma >= 0) {
        point = lastComma > lastDot ? QLatin1Char(',') : QLatin1Char('.');
    } else if (lastComma >= 0) {
        // One comma followed by 1–2 digits reads as a decimal comma;
        // "1,234" and "1,234,567" are grouping.
        const qsizetype after = digits.size() - lastComma - 1;
        point = digits.count(QLatin1Char(',')) == 1 && after > 0 && after != 3 ? QLatin1Char(',') : QLatin1Char('.');
    } else if (digits.count(QLatin1Char('.')) > 1) {
        point = QLatin1Char(','); // "1.234.567": dots are grouping
    }
    const QChar group = point == QLatin1Char('.') ? QLatin1Char(',') : QLatin1Char('.');
    digits.remove(group);
    if (digits.count(point) > 1) {
        return std::nullopt;
    }
    const qsizetype at = digits.indexOf(point);
    QString whole = at < 0 ? digits : digits.left(at);
    QString fraction = at < 0 ? QString() : digits.mid(at + 1);
    while (fraction.size() > exponent && fraction.endsWith(QLatin1Char('0'))) {
        fraction.chop(1);
    }
    if (fraction.size() > exponent) {
        return std::nullopt;
    }
    fraction = fraction.leftJustified(exponent, QLatin1Char('0'));
    if (whole.isEmpty()) {
        whole = QStringLiteral("0");
    }
    qint64 value = 0;
    for (const QChar c : whole + fraction) {
        if (qMulOverflow(value, qint64(10), &value) || qAddOverflow(value, qint64(c.unicode() - '0'), &value)) {
            return std::nullopt;
        }
    }
    return negative ? -value : value;
}

namespace {

QString stripTime(const QString &text)
{
    static const QRegularExpression isoTime(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})[T ].*$"));
    const QRegularExpressionMatch m = isoTime.match(text.trimmed());
    return m.hasMatch() ? m.captured(1) : text.trimmed();
}

QStringList candidates(bool dayFirst)
{
    const QStringList iso{QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/MM/dd"), QStringLiteral("yyyyMMdd"),
                          QStringLiteral("yyyy.MM.dd")};
    const QStringList monthFirst{QStringLiteral("MM/dd/yyyy"), QStringLiteral("M/d/yyyy"), QStringLiteral("MM-dd-yyyy"),
                                 QStringLiteral("MM/dd/yy"), QStringLiteral("M/d/yy")};
    const QStringList dayFirstList{QStringLiteral("dd/MM/yyyy"), QStringLiteral("d/M/yyyy"), QStringLiteral("dd-MM-yyyy"),
                                   QStringLiteral("dd/MM/yy"), QStringLiteral("d/M/yy")};
    const QStringList dotted{QStringLiteral("dd.MM.yyyy"), QStringLiteral("d.M.yyyy"), QStringLiteral("dd.MM.yy")};
    const QStringList named{QStringLiteral("d MMM yyyy"), QStringLiteral("dd MMM yyyy"), QStringLiteral("d-MMM-yyyy"),
                            QStringLiteral("dd-MMM-yyyy"), QStringLiteral("d-MMM-yy"), QStringLiteral("MMM d, yyyy"),
                            QStringLiteral("MMM d yyyy"), QStringLiteral("d MMMM yyyy"), QStringLiteral("MMMM d, yyyy")};
    return iso + (dayFirst ? dayFirstList + monthFirst : monthFirst + dayFirstList) + dotted + named;
}

} // namespace

QDate parseDate(const QString &text, const QString &format)
{
    const QString t = stripTime(text);
    if (t.isEmpty() || format.isEmpty()) {
        return {};
    }
    // Two-digit years read as 2000–2099 (bank exports are recent).
    QDate d = QDate::fromString(t, format, 2000);
    if (!d.isValid()) {
        // English month names whatever the system locale.
        d = QLocale::c().toDate(t, format, 2000);
    }
    return d;
}

QString detectDateFormat(const QStringList &samples, bool dayFirst)
{
    QStringList values;
    for (const QString &s : samples) {
        if (!s.trimmed().isEmpty()) {
            values << s;
        }
    }
    if (values.isEmpty()) {
        return {};
    }
    for (const QString &format : candidates(dayFirst)) {
        bool all = true;
        for (const QString &v : std::as_const(values)) {
            if (!parseDate(v, format).isValid()) {
                all = false;
                break;
            }
        }
        if (all) {
            return format;
        }
    }
    return {};
}

BankParseResult parseFile(const QString &path, int exponent, bool dayFirst, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }
    const QByteArray data = file.readAll();
    const QByteArray head = data.left(4096).toUpper();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (head.contains("<OFX") || head.contains("OFXHEADER") || suffix == QLatin1String("ofx")
        || suffix == QLatin1String("qfx")) {
        return parseOfx(data, exponent);
    }
    if (head.trimmed().startsWith("!TYPE:") || head.trimmed().startsWith("!ACCOUNT") || suffix == QLatin1String("qif")) {
        return parseQif(data, exponent, dayFirst);
    }
    return parseCsv(data, exponent, dayFirst);
}

} // namespace QindaOffice::Books::BankImport
