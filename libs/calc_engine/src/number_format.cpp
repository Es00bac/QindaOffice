// SPDX-License-Identifier: GPL-3.0-or-later
#include "number_format.h"

#include "functions/functions.h"

#include <QDate>

#include <QMap>
#include <QRegularExpression>

namespace QindaOffice::Calc {

namespace {

QString generalNumber(double value)
{
    // General: integers with up to 11 significant digits print plainly,
    // anything wider goes scientific (plan section 7.2).
    if (qFuzzyIsNull(value - std::round(value)) && std::abs(value) < 1e11) {
        return QString::number(qlonglong(std::llround(value)));
    }
    return QString::number(value, 'g', 11);
}

QString padInt(qint64 v, int width)
{
    return QString::number(v).rightJustified(width, QLatin1Char('0'));
}

// Expands a date/time pattern left to right. 'm' means month unless the
// token directly follows an hour token (Excel's ambiguous-minute rule).
// Removes [bracketed] and "quoted" spans so a letter inside [Red] or a
// literal is never mistaken for a date token.
QString stripDecorations(const QString &raw)
{
    QString out;
    for (int i = 0; i < raw.size();) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('"')) {
            const int end = raw.indexOf(QLatin1Char('"'), i + 1);
            i = end < 0 ? raw.size() : end + 1;
        } else if (c == QLatin1Char('[')) {
            const int end = raw.indexOf(QLatin1Char(']'), i);
            i = end < 0 ? raw.size() : end + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

QString formatDatePattern(const QString &pattern, const QDate &date, double serial)
{
    QString out;
    int i = 0;
    bool afterHour = false;
    // AGENT-NOTE: Excel treats an m token adjacent to an s token (either
    // side) as minutes, so "mm:ss" formats the time of day, not month:second.
    // A forward scan for the next letter token decides the context.
    const auto nextLetterTokenIsSeconds = [&pattern](int from) {
        for (int j = from; j < pattern.size(); ++j) {
            const QChar ch = pattern.at(j);
            if (ch == QLatin1Char('[') || ch == QLatin1Char('"')) {
                // skip bracketed/quoted spans entirely
                const QChar close = ch == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('"');
                const int end = pattern.indexOf(close, j + 1);
                j = end < 0 ? pattern.size() - 1 : end;
                continue;
            }
            if (ch.isLetter()) {
                return ch.toLower() == QLatin1Char('s');
            }
        }
        return false;
    };
    const auto prevLetterTokenWasSeconds = [&pattern](int before) {
        for (int j = before - 1; j >= 0; --j) {
            const QChar ch = pattern.at(j);
            if (ch == QLatin1Char(']')) {
                // skip backwards over the whole bracketed span
                const int open = pattern.lastIndexOf(QLatin1Char('['), j - 1);
                j = open < 0 ? 0 : open;
                continue;
            }
            if (ch == QLatin1Char('"')) {
                const int open = pattern.lastIndexOf(QLatin1Char('"'), j - 1);
                j = open < 0 ? 0 : open;
                continue;
            }
            if (ch.isLetter()) {
                return ch.toLower() == QLatin1Char('s');
            }
        }
        return false;
    };
    while (i < pattern.size()) {
        const QChar c = pattern.at(i);
        // Bracketed duration units are single tokens, not letter runs.
        if (c == QLatin1Char('[')) {
            const int end = pattern.indexOf(QLatin1Char(']'), i);
            if (end > i) {
                const QString inner = pattern.mid(i + 1, end - i - 1).toLower();
                if (inner == QLatin1String("h")) {
                    out += QString::number(qint64(serial * 24.0));
                } else if (inner == QLatin1String("mm")) {
                    out += padInt(qint64(serial * 24.0 * 60.0), 2);
                } else if (inner == QLatin1String("ss")) {
                    out += padInt(qint64(serial * 24.0 * 3600.0), 2);
                } else {
                    out += pattern.mid(i, end - i + 1);
                }
                afterHour = inner == QLatin1String("h");
                i = end + 1;
                continue;
            }
        }
        // AM/PM is a unit token, not a letter run.
        if (pattern.mid(i, 5).compare(QLatin1String("AM/PM"), Qt::CaseInsensitive) == 0) {
            const int hour24 = qint64(serial * 24.0) % 24;
            const int hour12 = hour24 == 0 ? 12 : (hour24 > 12 ? hour24 - 12 : hour24);
            out += hour12 < 12 ? QStringLiteral("AM") : QStringLiteral("PM");
            i += 5;
            continue;
        }
        if (c == QLatin1Char('"')) {
            const int end = pattern.indexOf(QLatin1Char('"'), i + 1);
            out += pattern.mid(i + 1, (end < 0 ? pattern.size() : end) - i - 1);
            i = end < 0 ? pattern.size() : end + 1;
            continue;
        }
        int run = 1;
        while (i + run < pattern.size() && pattern.at(i + run) == c) {
            ++run;
        }
        const QString token = pattern.mid(i, run);
        const QString lower = token.toLower();
        // m reads as minutes when an hour token precedes it or a seconds
        // token neighbours it (either side); otherwise it is the month.
        const bool minuteContext = afterHour
            || (lower.startsWith(QLatin1Char('m'))
                && (nextLetterTokenIsSeconds(i + run) || prevLetterTokenWasSeconds(i)));
        if (lower == QLatin1String("yyyy")) out += padInt(date.year(), 4);
        else if (lower == QLatin1String("yy")) out += padInt(date.year() % 100, 2);
        else if (lower == QLatin1String("mmmm")) out += date.toString(QStringLiteral("MMMM"));
        else if (lower == QLatin1String("mmm")) out += date.toString(QStringLiteral("MMM"));
        else if (lower == QLatin1String("mm") && !minuteContext) out += padInt(date.month(), 2);
        else if (lower == QLatin1String("m") && !minuteContext) out += QString::number(date.month());
        else if (lower == QLatin1String("mm")) out += padInt(qint64(serial * 24.0 * 60.0) % 60, 2);
        else if (lower == QLatin1String("m")) out += QString::number(qint64(serial * 24.0 * 60.0) % 60);
        else if (lower == QLatin1String("dddd")) out += date.toString(QStringLiteral("dddd"));
        else if (lower == QLatin1String("ddd")) out += date.toString(QStringLiteral("ddd"));
        else if (lower == QLatin1String("dd")) out += padInt(date.day(), 2);
        else if (lower == QLatin1String("d")) out += QString::number(date.day());
        else if (lower == QLatin1String("hh")) {
            const int hour24 = qint64(serial * 24.0) % 24;
            const int hour12 = hour24 == 0 ? 12 : (hour24 > 12 ? hour24 - 12 : hour24);
            out += padInt(hour12, 2);
        }
        else if (lower == QLatin1String("h")) {
            const int hour24 = qint64(serial * 24.0) % 24;
            const int hour12 = hour24 == 0 ? 12 : (hour24 > 12 ? hour24 - 12 : hour24);
            out += QString::number(hour12);
        }
        else if (lower == QLatin1String("ss")) out += padInt(qint64(serial * 24.0 * 3600.0) % 60, 2);
        else if (lower == QLatin1String("s")) out += QString::number(qint64(serial * 24.0 * 3600.0) % 60);
        else if (token.compare(QLatin1String("AM/PM"), Qt::CaseInsensitive) == 0) {
            const int hour24 = qint64(serial * 24.0) % 24;
            const int hour12 = hour24 == 0 ? 12 : (hour24 > 12 ? hour24 - 12 : hour24);
            out += hour12 < 12 ? QStringLiteral("AM") : QStringLiteral("PM");
        } else if (lower.startsWith(QLatin1Char('[')) && lower.endsWith(QLatin1Char(']'))) {
            const QString inner = lower.mid(1, lower.size() - 2);
            if (inner == QLatin1String("h")) out += QString::number(qint64(serial * 24.0));
            else if (inner == QLatin1String("mm")) out += padInt(qint64(serial * 24.0 * 60.0), 2);
            else if (inner == QLatin1String("ss")) out += padInt(qint64(serial * 24.0 * 3600.0), 2);
        } else {
            out += token;
        }
        // Only letter tokens may flip the minute/month context; punctuation
        // and literals preserve it (so "h:mm" reads minutes, not months).
        if (token.at(0).isLetter()) {
            afterHour = lower.startsWith(QLatin1Char('h'))
                && lower != QLatin1String("am/pm");
        }
        i += run;
    }
    return out;
}

} // namespace

NumberFormat::NumberFormat() = default;

NumberFormat::NumberFormat(const QString &code)
{
    compile(code);
}

QString NumberFormat::builtinCode(int numFmtId)
{
    static const QMap<int, QString> kBuiltin = {
        {0, QStringLiteral("General")}, {1, QStringLiteral("0")}, {2, QStringLiteral("0.00")},
        {3, QStringLiteral("#,##0")}, {4, QStringLiteral("#,##0.00")},
        {9, QStringLiteral("0%")}, {10, QStringLiteral("0.00%")},
        {11, QStringLiteral("0.00E+00")}, {12, QStringLiteral("# ?/?")}, {13, QStringLiteral("# ??/??")},
        {14, QStringLiteral("mm-dd-yy")}, {15, QStringLiteral("d-mmm-yy")}, {16, QStringLiteral("d-mmm")},
        {17, QStringLiteral("mmm-yy")}, {18, QStringLiteral("h:mm AM/PM")}, {19, QStringLiteral("h:mm:ss AM/PM")},
        {20, QStringLiteral("h:mm")}, {21, QStringLiteral("h:mm:ss")}, {22, QStringLiteral("m/d/yy h:mm")},
        {37, QStringLiteral("#,##0 ;(#,##0)")}, {38, QStringLiteral("#,##0 ;[Red](#,##0)")},
        {39, QStringLiteral("#,##0.00;(#,##0.00)")}, {40, QStringLiteral("#,##0.00;[Red](#,##0.00)")},
        {45, QStringLiteral("mm:ss")}, {46, QStringLiteral("[h]:mm:ss")}, {47, QStringLiteral("mmss.0")},
        {48, QStringLiteral("##0.0E+0")}, {49, QStringLiteral("@")},
    };
    return kBuiltin.value(numFmtId, QStringLiteral("General"));
}

bool NumberFormat::isGeneral() const
{
    return m_code.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0 || m_code.isEmpty();
}

void NumberFormat::compile(const QString &code)
{
    m_code = code;
    m_valid = true;
    m_sections.clear();
    // Split into sections on ';' that are not inside quotes or brackets.
    QStringList rawSections;
    QString current;
    bool inQuote = false;
    int bracketDepth = 0;
    for (const QChar c : code) {
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
        } else if (!inQuote && c == QLatin1Char('[')) {
            ++bracketDepth;
        } else if (!inQuote && c == QLatin1Char(']')) {
            bracketDepth = qMax(0, bracketDepth - 1);
        }
        if (c == QLatin1Char(';') && !inQuote && bracketDepth == 0) {
            rawSections.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    rawSections.append(current);
    for (const QString &raw : rawSections) {
        Section section;
        section.raw = raw;
        static const QRegularExpression kDateToken(
            QStringLiteral("y{1,4}|m{1,5}|d{1,4}|h{1,2}|s{1,2}|AM/PM|\\[h\\]|\\[mm\\]|\\[ss\\]"),
            QRegularExpression::CaseInsensitiveOption);
        if (raw.contains(QLatin1Char('@'))) {
            section.isText = true;
            // Everything before '@' prefixes the text, everything after
            // suffixes it; the placeholder itself is implicit.
            const int at = raw.indexOf(QLatin1Char('@'));
            section.prefix = raw.left(at);
            section.suffix = raw.mid(at + 1);
        } else if (kDateToken.match(stripDecorations(raw)).hasMatch()) {
            section.isDate = true;
            section.datePattern = raw;
        } else {
            // Scan tokens: literal text in quotes, escapes, colour/currency
            // in brackets, then digit grammar.
            QString digits;
            for (int i = 0; i < raw.size();) {
                const QChar c = raw.at(i);
                if (c == QLatin1Char('"')) {
                    const int end = raw.indexOf(QLatin1Char('"'), i + 1);
                    const QString lit = raw.mid(i + 1, (end < 0 ? raw.size() : end) - i - 1);
                    if (digits.isEmpty() && section.decimals <= 0 && section.integerDigits == 0) {
                        section.prefix += lit;
                    } else {
                        section.suffix += lit;
                    }
                    i = end < 0 ? raw.size() : end + 1;
                } else if (c == QLatin1Char('\\')) {
                    const QString lit = raw.mid(i + 1, 1);
                    if (section.integerDigits == 0 && section.decimals <= 0) {
                        section.prefix += lit;
                    } else {
                        section.suffix += lit;
                    }
                    i += 2;
                } else if (c == QLatin1Char('[')) {
                    const int end = raw.indexOf(QLatin1Char(']'), i);
                    const QString inner = raw.mid(i + 1, (end < 0 ? raw.size() : end) - i - 1);
                    if (inner.startsWith(QLatin1String("Red"), Qt::CaseInsensitive)) {
                        section.colourToken = QStringLiteral("red");
                    }
                    i = end < 0 ? raw.size() : end + 1;
                } else if (c == QLatin1Char('_') || c == QLatin1Char('*')) {
                    i += 2; // spacing/fill tokens are dropped in v1
                } else if ((c == QLatin1Char('+') || c == QLatin1Char('-'))
                           && !(!digits.isEmpty()
                                && (digits.endsWith(QLatin1Char('E'))
                                    || digits.endsWith(QLatin1Char('e'))))) {
                    // A bare sign is a displayed literal (Excel rule), except
                    // right after E/e where it is the exponent sign. Parens
                    // from accounting formats like "(0.00)" are literals too.
                    if (digits.isEmpty()) {
                        section.prefix += c;
                    } else {
                        section.suffix += c;
                    }
                    ++i;
                } else if (c == QLatin1Char('(') || c == QLatin1Char(')')) {
                    if (digits.isEmpty()) {
                        section.prefix += c;
                    } else {
                        section.suffix += c;
                    }
                    ++i;
                } else if (c.isSpace()
                           || (c.isLetter() && c != QLatin1Char('E')
                               && c != QLatin1Char('e'))) {
                    // Whitespace and non-token letters are literals (Excel
                    // shows a space or the letter); they must not pad the
                    // digit grammar either.
                    if (digits.isEmpty()) {
                        section.prefix += c;
                    } else {
                        section.suffix += c;
                    }
                    ++i;
                } else {
                    digits += c;
                    ++i;
                }
            }
            // Count only digit tokens; '%' and 'E' decoration must not pad
            // the decimal width.
            QString digitOnly = digits;
            digitOnly.remove(QLatin1Char('%'));
            static const QRegularExpression kExpTail(
                QStringLiteral("[Ee][+-]?[0#?]+"));
            digitOnly.remove(kExpTail);
            const QString pattern = digitOnly;
            if (pattern.contains(QLatin1Char('.'))) {
                const int dot = pattern.indexOf(QLatin1Char('.'));
                const QString frac = pattern.mid(dot + 1);
                section.decimals = frac.count(QLatin1Char('0')) + frac.count(QLatin1Char('#'))
                    + frac.count(QLatin1Char('?'));
                section.integerDigits = pattern.left(dot).count(QLatin1Char('0'))
                    + pattern.left(dot).count(QLatin1Char('#')) + pattern.left(dot).count(QLatin1Char('?'));
            } else {
                section.decimals = 0;
                section.integerDigits = pattern.count(QLatin1Char('0')) + pattern.count(QLatin1Char('#'))
                    + pattern.count(QLatin1Char('?'));
            }
            section.hasThousands = pattern.contains(QLatin1Char(','));
            section.percent = digits.contains(QLatin1Char('%'));
            section.scientific = digits.contains(QLatin1Char('E'), Qt::CaseInsensitive);
        }
        m_sections.append(section);
    }
    if (m_sections.isEmpty()) {
        m_valid = false;
    }
}

FormattedText NumberFormat::formatNumber(double number) const
{
    if (!m_valid) {
        return {generalNumber(number), std::nullopt};
    }
    if (isGeneral()) {
        return {generalNumber(number), std::nullopt};
    }
    const int sectionCount = m_sections.size();
    const Section *section = nullptr;
    if (number > 0 || (number == 0 && sectionCount < 3)) {
        section = &m_sections.first();
    } else if (number < 0 && sectionCount >= 2) {
        section = &m_sections.at(1);
    } else {
        section = &m_sections.at(qMin(2, sectionCount - 1));
    }
    FormattedText out;
    if (section->colourToken.compare(QLatin1String("red"), Qt::CaseInsensitive) == 0) {
        out.colour = Rgb{255, 0, 0};
    }
    if (section->isDate) {
        out.text = section->prefix
            + formatDatePattern(section->datePattern, dateFromSerial(std::abs(number)),
                                std::abs(number))
            + section->suffix;
        return out;
    }
    // A section with no digit tokens at all is a literal (e.g. "zero").
    if (section->integerDigits == 0 && section->decimals == 0) {
        out.text = section->prefix + section->suffix;
        return out;
    }
    double value = number;
    // For the negative section, use absolute value (Excel behavior)
    if (number < 0 && sectionCount >= 2 && section == &m_sections.at(1)) {
        value = std::abs(number);
    }
    if (section->percent) {
        value *= 100.0;
    }
    if (section->scientific) {
        out.text = section->prefix + QString::number(value, 'E', qMax(0, section->decimals))
                   + section->suffix;
        return out;
    }
    QString digits;
    if (section->decimals > 0) {
        digits = QString::number(value, 'f', section->decimals);
    } else {
        digits = QString::number(std::llround(value));
    }
    if (section->hasThousands) {
        const int dot = digits.indexOf(QLatin1Char('.'));
        const int end = dot < 0 ? digits.size() : dot;
        for (int i = end - 3; i > 0; i -= 3) {
            digits.insert(i, QLatin1Char(','));
        }
    }
    out.text = section->prefix + digits + (section->percent ? QStringLiteral("%") : QString())
               + section->suffix;
    return out;
}

FormattedText NumberFormat::formatText(const QString &text) const
{
    const Section *section = nullptr;
    for (const Section &candidate : m_sections) {
        if (candidate.isText) {
            section = &candidate;
            break;
        }
    }
    if (section == nullptr) {
        return {text, std::nullopt};
    }
    FormattedText out;
    if (section->colourToken.compare(QLatin1String("red"), Qt::CaseInsensitive) == 0) {
        out.colour = Rgb{255, 0, 0};
    }
    out.text = section->prefix + text + section->suffix;
    return out;
}

FormattedText NumberFormat::format(const Value &value) const
{
    if (const auto *d = std::get_if<double>(&value)) {
        return formatNumber(*d);
    }
    if (const auto *s = std::get_if<QString>(&value)) {
        return formatText(*s);
    }
    if (const auto *b = std::get_if<bool>(&value)) {
        return formatText(*b ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
    }
    if (const auto *e = std::get_if<Err>(&value)) {
        return {errText(*e), std::nullopt};
    }
    return {QString(), std::nullopt};
}

FormattedText formatWithCode(const Value &value, const QString &code)
{
    return NumberFormat(code).format(value);
}

} // namespace QindaOffice::Calc
