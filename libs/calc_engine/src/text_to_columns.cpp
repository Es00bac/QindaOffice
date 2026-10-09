// SPDX-License-Identifier: GPL-3.0-or-later
// Text to Columns (text_to_columns.h, M78, D-087): Excel's Convert Text to
// Columns Wizard as pure functions — the split (step 2) and the column data
// formats (step 3) — producing the cell inputs the app applies as one undo
// step.

#include "text_to_columns.h"

#include "functions/functions.h"

#include <QDate>
#include <QRegularExpression>
#include <QTime>

#include <algorithm>
#include <optional>

namespace QindaOffice::Calc {

namespace {

using Format = TextToColumnsOptions::Format;

bool isDelimiter(QChar c, const TextToColumnsOptions &options)
{
    return (options.tab && c == QLatin1Char('\t')) || (options.semicolon && c == QLatin1Char(';'))
        || (options.comma && c == QLatin1Char(',')) || (options.space && c == QLatin1Char(' '))
        || (!options.other.isEmpty() && c == options.other.at(0));
}

// Step 2, delimited. A field that starts with the qualifier runs to the
// qualifier that closes it (doubled qualifiers inside are one), so the
// delimiters it holds are text; anything after the closing qualifier up to
// the next delimiter joins the field. A line ending in a delimiter has an
// empty last field, as in Excel.
QStringList splitDelimited(const QString &line, const TextToColumnsOptions &options)
{
    QStringList fields;
    const QChar qualifier = options.qualifier;
    const qsizetype n = line.size();
    qsizetype i = 0;
    while (true) {
        QString field;
        if (!qualifier.isNull() && i < n && line.at(i) == qualifier) {
            ++i;
            while (i < n) {
                if (line.at(i) == qualifier) {
                    if (i + 1 < n && line.at(i + 1) == qualifier) {
                        field += qualifier;
                        i += 2;
                        continue;
                    }
                    ++i; // the closing qualifier
                    break;
                }
                field += line.at(i++);
            }
        }
        while (i < n && !isDelimiter(line.at(i), options)) {
            field += line.at(i++);
        }
        fields.append(field);
        if (i >= n) {
            break;
        }
        ++i; // the delimiter
        if (options.consecutiveAsOne) {
            while (i < n && isDelimiter(line.at(i), options)) {
                ++i;
            }
        }
        if (i >= n) {
            fields.append(QString());
            break;
        }
    }
    return fields;
}

QList<int> cleanBreaks(const QList<int> &breaks)
{
    QList<int> out;
    for (const int position : breaks) {
        if (position > 0 && !out.contains(position)) {
            out.append(position);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Step 2, fixed width: the text between the breaks, without the padding
// spaces Excel's preview shows around it.
QStringList splitFixed(const QString &line, const QList<int> &breaks)
{
    QStringList fields;
    int start = 0;
    for (const int position : breaks) {
        fields.append(line.mid(start, position - start).trimmed());
        start = position;
    }
    fields.append(line.mid(start).trimmed());
    return fields;
}

// A number as General reads it with step 3's Advanced settings: thousands
// separators dropped, the decimal separator made '.', a trailing minus or
// parentheses for a negative, a leading or trailing currency sign, an
// exponent, a percent sign kept for the engine (it makes 12% 0.12). The
// text the engine's input reader takes, or nullopt.
std::optional<QString> generalNumber(const QString &field, const TextToColumnsOptions &options)
{
    QString text = field.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool negative = false;
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        negative = true;
        text = text.mid(1, text.size() - 2).trimmed();
    }
    if (options.trailingMinus && text.size() > 1 && text.endsWith(QLatin1Char('-'))) {
        negative = !negative;
        text.chop(1);
    }
    static const QString currency = QStringLiteral("$€£¥");
    if (!text.isEmpty() && currency.contains(text.front())) {
        text.remove(0, 1);
    } else if (!text.isEmpty() && currency.contains(text.back())) {
        text.chop(1);
    }
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('-')) || text.startsWith(QLatin1Char('+'))) {
        negative = text.front() == QLatin1Char('-') ? !negative : negative;
        text.remove(0, 1);
    }
    bool percent = false;
    if (text.endsWith(QLatin1Char('%'))) {
        percent = true;
        text.chop(1);
    }
    // Thousands separators only before the decimal separator and the
    // exponent; the decimal separator becomes '.'.
    QString normalized;
    bool decimalSeen = false;
    bool exponentSeen = false;
    bool digits = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isDigit()) {
            normalized += c;
            digits = true;
        } else if (c == options.decimalSeparator && !decimalSeen && !exponentSeen) {
            normalized += QLatin1Char('.');
            decimalSeen = true;
        } else if (c == options.thousandsSeparator && !decimalSeen && !exponentSeen && digits) {
            continue;
        } else if ((c == QLatin1Char('e') || c == QLatin1Char('E')) && digits && !exponentSeen) {
            normalized += QLatin1Char('E');
            exponentSeen = true;
            if (i + 1 < text.size() && (text.at(i + 1) == QLatin1Char('-') || text.at(i + 1) == QLatin1Char('+'))) {
                normalized += text.at(++i);
            }
        } else {
            return std::nullopt;
        }
    }
    bool ok = false;
    static_cast<void>(normalized.toDouble(&ok));
    if (!ok || !digits) {
        return std::nullopt;
    }
    return (negative ? QStringLiteral("-") : QString()) + normalized + (percent ? QStringLiteral("%") : QString());
}

int monthFromName(const QString &text)
{
    static const QStringList names{QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("mar"),
                                   QStringLiteral("apr"), QStringLiteral("may"), QStringLiteral("jun"),
                                   QStringLiteral("jul"), QStringLiteral("aug"), QStringLiteral("sep"),
                                   QStringLiteral("oct"), QStringLiteral("nov"), QStringLiteral("dec")};
    const QString lower = text.toLower();
    if (lower.size() < 3) {
        return 0;
    }
    for (int m = 0; m < names.size(); ++m) {
        if (lower.startsWith(names.at(m))) {
            return m + 1;
        }
    }
    return 0;
}

// Excel's two-digit years: 00–29 are 2000–2029, 30–99 are 1930–1999.
int fullYear(const QString &digits)
{
    const int year = digits.toInt();
    if (digits.size() <= 2) {
        return year < 30 ? 2000 + year : 1900 + year;
    }
    return digits.size() == 4 ? year : -1;
}

struct DateValue {
    double serial = 0;
    bool time = false;
};

// A date in the given order of day, month and year ("MDY" …), with an
// optional time after it; nullopt when it is not one.
std::optional<DateValue> parseDate(const QString &field, const QString &order)
{
    QString text = field.trimmed();
    static const QRegularExpression timeTail(
        QStringLiteral("[ T]+(\\d{1,2}):(\\d{2})(?::(\\d{2}(?:\\.\\d+)?))?\\s*([AaPp][Mm])?$"));
    double fraction = 0;
    bool hasTime = false;
    if (const QRegularExpressionMatch time = timeTail.match(text); time.hasMatch()) {
        int hour = time.captured(1).toInt();
        const int minute = time.captured(2).toInt();
        const double second = time.captured(3).toDouble();
        const QString meridiem = time.captured(4).toLower();
        if (!meridiem.isEmpty()) {
            if (hour < 1 || hour > 12) {
                return std::nullopt;
            }
            hour = hour % 12 + (meridiem == QLatin1String("pm") ? 12 : 0);
        }
        if (hour > 23 || minute > 59 || second >= 60) {
            return std::nullopt;
        }
        fraction = (hour * 3600 + minute * 60 + second) / 86400.0;
        hasTime = true;
        text = text.left(time.capturedStart()).trimmed();
    }
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    const QStringList parts = text.split(separators, Qt::SkipEmptyParts);
    if (parts.size() != 3) {
        return std::nullopt;
    }
    int day = 0;
    int month = 0;
    int year = -1;
    for (int i = 0; i < 3; ++i) {
        const QString &part = parts.at(i);
        const bool numeric = std::all_of(part.cbegin(), part.cend(), [](QChar c) { return c.isDigit(); });
        switch (order.at(i).toLatin1()) {
        case 'D':
            day = numeric && part.size() <= 2 ? part.toInt() : 0;
            break;
        case 'M':
            month = numeric ? (part.size() <= 2 ? part.toInt() : 0) : monthFromName(part);
            break;
        default:
            year = numeric ? fullYear(part) : -1;
            break;
        }
    }
    const QDate date(year, month, day);
    if (year < 0 || !date.isValid()) {
        return std::nullopt;
    }
    return DateValue{serialFromDate(date) + fraction, hasTime};
}

QString dateOrder(Format format)
{
    switch (format) {
    case Format::DateMDY: return QStringLiteral("MDY");
    case Format::DateDMY: return QStringLiteral("DMY");
    case Format::DateYMD: return QStringLiteral("YMD");
    case Format::DateMYD: return QStringLiteral("MYD");
    case Format::DateDYM: return QStringLiteral("DYM");
    case Format::DateYDM: return QStringLiteral("YDM");
    default: return {};
    }
}

QString serialText(double serial)
{
    return QString::number(serial, 'g', 15);
}

// One field in its column's format.
TextColumnCell convertField(const QString &field, Format format, const TextToColumnsOptions &options)
{
    TextColumnCell cell;
    if (field.isEmpty()) {
        return cell; // an empty field clears its cell
    }
    if (format == Format::Text) {
        cell.input = QLatin1Char('\'') + field;
        return cell;
    }
    if (format != Format::General) {
        if (const auto date = parseDate(field, dateOrder(format))) {
            cell.input = serialText(date->serial);
            cell.dateFormat = true;
            cell.numberFormat = date->time ? QStringLiteral("m/d/yyyy h:mm") : QStringLiteral("m/d/yyyy");
        } else {
            cell.input = QLatin1Char('\'') + field; // not a date in this order: it stays text
        }
        return cell;
    }
    if (const auto number = generalNumber(field, options)) {
        cell.input = *number;
        if (number->endsWith(QLatin1Char('%'))) {
            cell.numberFormat = number->contains(QLatin1Char('.')) ? QStringLiteral("0.00%") : QStringLiteral("0%");
        }
        return cell;
    }
    // General takes dates the way Excel's US settings read them: ISO
    // (2026-01-02) and month/day/year.
    static const QRegularExpression isoDate(QStringLiteral("^\\d{4}[-/]\\d{1,2}[-/]\\d{1,2}$"));
    static const QRegularExpression usDate(QStringLiteral("^\\d{1,2}/\\d{1,2}/(\\d{2}|\\d{4})(\\s.*)?$"));
    const QString trimmed = field.trimmed();
    std::optional<DateValue> date;
    if (isoDate.match(trimmed).hasMatch()) {
        date = parseDate(trimmed, QStringLiteral("YMD"));
    } else if (usDate.match(trimmed).hasMatch()) {
        date = parseDate(trimmed, QStringLiteral("MDY"));
    }
    if (date.has_value()) {
        cell.input = serialText(date->serial);
        cell.dateFormat = true;
        cell.numberFormat = date->time ? QStringLiteral("m/d/yyyy h:mm") : QStringLiteral("m/d/yyyy");
        return cell;
    }
    // A leading apostrophe is the field's own character, not the input
    // reader's text marker; anything else follows the engine's input rules
    // (TRUE, a formula after '=', as in Excel).
    cell.input = field.startsWith(QLatin1Char('\'')) ? QLatin1Char('\'') + field : field;
    return cell;
}

} // namespace

QList<QStringList> splitTextToColumns(const QStringList &lines, const TextToColumnsOptions &options)
{
    QList<QStringList> out;
    out.reserve(lines.size());
    const QList<int> breaks = cleanBreaks(options.breaks);
    for (const QString &line : lines) {
        out.append(options.mode == TextToColumnsOptions::Mode::FixedWidth ? splitFixed(line, breaks)
                                                                           : splitDelimited(line, options));
    }
    return out;
}

QList<int> guessFixedWidthBreaks(const QStringList &lines)
{
    qsizetype width = 0;
    for (const QString &line : lines) {
        width = qMax(width, line.size());
    }
    // A column is blank when every non-empty line has a space there (or
    // ends before it); a break goes where text starts again after one.
    QList<bool> blank(width, true);
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        for (qsizetype c = 0; c < line.size(); ++c) {
            if (!line.at(c).isSpace()) {
                blank[c] = false;
            }
        }
    }
    QList<int> breaks;
    bool seenText = false;
    for (qsizetype c = 0; c < width; ++c) {
        if (blank.at(c)) {
            continue;
        }
        if (seenText && c > 0 && blank.at(c - 1)) {
            breaks.append(int(c));
        }
        seenText = true;
    }
    return breaks;
}

QList<TextColumnCell> textToColumnsCells(const QStringList &lines, qint32 firstRow, const CellAddr &destination,
                                         const TextToColumnsOptions &options)
{
    QList<TextColumnCell> out;
    const QList<QStringList> split = splitTextToColumns(lines, options);
    // AGENT-NOTE: the first line lands on destination's row (Excel's
    // Destination box); a destination without a row means "beside the
    // source", the row of the first line.
    const qint32 top = destination.row >= 0 ? destination.row : firstRow;
    for (qsizetype i = 0; i < split.size(); ++i) {
        if (lines.at(i).isEmpty()) {
            continue; // an empty source cell gives nothing to write
        }
        qint32 column = destination.col;
        const QStringList &fields = split.at(i);
        for (qsizetype f = 0; f < fields.size(); ++f) {
            const Format format = options.formats.value(f, Format::General);
            if (format == Format::Skip) {
                continue; // dropped; the columns after it close up
            }
            TextColumnCell cell = convertField(fields.at(f), format, options);
            cell.addr = {top + qint32(i), column++};
            out.append(cell);
        }
    }
    return out;
}

int textToColumnsWidth(const QStringList &lines, const TextToColumnsOptions &options)
{
    int width = 0;
    const QList<QStringList> split = splitTextToColumns(lines, options);
    for (qsizetype i = 0; i < split.size(); ++i) {
        if (lines.at(i).isEmpty()) {
            continue;
        }
        int kept = 0;
        for (qsizetype f = 0; f < split.at(i).size(); ++f) {
            kept += options.formats.value(f, Format::General) == Format::Skip ? 0 : 1;
        }
        width = qMax(width, kept);
    }
    return width;
}

} // namespace QindaOffice::Calc
