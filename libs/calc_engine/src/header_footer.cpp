// SPDX-License-Identifier: GPL-3.0-or-later
// Header and footer codes (header_footer.h, M78, D-087): Excel's &-codes
// parsed into three sections of formatted runs, Page Setup's &[Page]
// spelling for editing, fields filled in for a page, and Excel's
// ready-made headers and footers.
//
// AGENT-NOTE: the file keeps the codes verbatim (Sheet::PageSetup holds the
// strings); parsing only serves drawing, so a code it cannot draw (a theme
// colour, &G's picture) costs nothing on save.

#include "header_footer.h"

namespace QindaOffice::Calc {

namespace {

// The formatting a run starts with; each section starts afresh.
struct Format {
    QString font;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool doubleUnderline = false;
    bool strikeout = false;
    bool superscript = false;
    bool subscript = false;
    double size = 0;
    std::optional<Rgb> color;
};

HeaderFooterRun runWith(const Format &format)
{
    HeaderFooterRun run;
    run.font = format.font;
    run.bold = format.bold;
    run.italic = format.italic;
    run.underline = format.underline;
    run.doubleUnderline = format.doubleUnderline;
    run.strikeout = format.strikeout;
    run.superscript = format.superscript;
    run.subscript = format.subscript;
    run.size = format.size;
    run.color = format.color;
    return run;
}

HeaderFooterRun::Field fieldFor(QChar code)
{
    using Field = HeaderFooterRun::Field;
    switch (code.unicode()) {
    case 'P': return Field::Page;
    case 'N': return Field::Pages;
    case 'D': return Field::Date;
    case 'T': return Field::Time;
    case 'F': return Field::File;
    case 'Z': return Field::Path;
    case 'A': return Field::Sheet;
    case 'G': return Field::Picture;
    default: return Field::None;
    }
}

// The editing spelling of each field code, and back.
struct FieldName {
    char code;
    const char *name;
};
constexpr FieldName kFieldNames[] = {{'P', "Page"}, {'N', "Pages"}, {'D', "Date"}, {'T', "Time"},
                                     {'F', "File"}, {'Z', "Path"},  {'A', "Tab"},  {'G', "Picture"}};

QString fieldDisplay(QChar code)
{
    for (const FieldName &field : kFieldNames) {
        if (code == QLatin1Char(field.code)) {
            return QStringLiteral("&[%1]").arg(QLatin1String(field.name));
        }
    }
    return {};
}

// The length of a code's argument that follows "&" + its letter at `at`
// (the index of the letter): a quoted font, the digits of a size, the six
// characters of a colour. Copied as they are both ways.
qsizetype argumentEnd(const QString &code, qsizetype at)
{
    const QChar letter = code.at(at);
    if (letter == QLatin1Char('"')) {
        const qsizetype close = code.indexOf(QLatin1Char('"'), at + 1);
        return close < 0 ? code.size() : close + 1;
    }
    if (letter.isDigit()) {
        qsizetype end = at;
        while (end < code.size() && end < at + 3 && code.at(end).isDigit()) {
            ++end;
        }
        return end;
    }
    if (letter == QLatin1Char('K')) {
        return qMin(code.size(), at + 7);
    }
    return at + 1;
}

std::optional<Rgb> hexColor(QStringView text)
{
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok || text.size() != 6) {
        return std::nullopt; // a theme colour ("01+000"): no RGB without the theme
    }
    return Rgb{quint8(value >> 16), quint8(value >> 8), quint8(value)};
}

} // namespace

HeaderFooterText parseHeaderFooter(const QString &code)
{
    HeaderFooterText out;
    HeaderFooterSection *section = &out.center; // text before any &L/&C/&R is centred
    Format format;
    QString text;
    const auto flush = [&] {
        if (!text.isEmpty()) {
            HeaderFooterRun run = runWith(format);
            run.text = text;
            section->runs.append(run);
            text.clear();
        }
    };
    for (qsizetype i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c != QLatin1Char('&') || i + 1 >= code.size()) {
            text += c;
            continue;
        }
        const QChar letter = code.at(++i);
        if (letter == QLatin1Char('&')) {
            text += QLatin1Char('&');
            continue;
        }
        if (letter == QLatin1Char('L') || letter == QLatin1Char('C') || letter == QLatin1Char('R')) {
            flush();
            section = letter == QLatin1Char('L') ? &out.left : letter == QLatin1Char('C') ? &out.center : &out.right;
            format = Format();
            continue;
        }
        if (const HeaderFooterRun::Field field = fieldFor(letter); field != HeaderFooterRun::Field::None) {
            flush();
            HeaderFooterRun run = runWith(format);
            run.field = field;
            // &P+n / &P-n: a page number offset.
            if (field == HeaderFooterRun::Field::Page && i + 2 < code.size()
                && (code.at(i + 1) == QLatin1Char('+') || code.at(i + 1) == QLatin1Char('-')) && code.at(i + 2).isDigit()) {
                qsizetype end = i + 2;
                while (end < code.size() && code.at(end).isDigit()) {
                    ++end;
                }
                run.pageOffset = code.mid(i + 1, end - i - 1).toInt();
                i = end - 1;
            }
            section->runs.append(run);
            continue;
        }
        flush();
        const qsizetype end = argumentEnd(code, i);
        switch (letter.unicode()) {
        case '"': {
            const QString spec = code.mid(i + 1, end - i - 2);
            const QString family = spec.section(QLatin1Char(','), 0, 0).trimmed();
            const QString style = spec.section(QLatin1Char(','), 1).trimmed();
            if (!family.isEmpty() && family != QLatin1String("-")) {
                format.font = family;
            }
            if (!style.isEmpty()) {
                format.bold = style.contains(QLatin1String("Bold"), Qt::CaseInsensitive);
                format.italic = style.contains(QLatin1String("Italic"), Qt::CaseInsensitive)
                    || style.contains(QLatin1String("Oblique"), Qt::CaseInsensitive);
            }
            break;
        }
        case 'K':
            format.color = hexColor(QStringView(code).mid(i + 1, end - i - 1));
            break;
        case 'B': format.bold = !format.bold; break;
        case 'I': format.italic = !format.italic; break;
        case 'U': format.underline = !format.underline; format.doubleUnderline = false; break;
        case 'E': format.doubleUnderline = !format.doubleUnderline; format.underline = false; break;
        case 'S': format.strikeout = !format.strikeout; break;
        case 'X': format.superscript = !format.superscript; format.subscript = false; break;
        case 'Y': format.subscript = !format.subscript; format.superscript = false; break;
        case 'O': // outline and shadow: Mac-only styles Excel for Windows ignores too
        case 'H':
            break;
        default:
            if (letter.isDigit()) {
                format.size = code.mid(i, end - i).toDouble();
            } else {
                text += QLatin1Char('&'); // not a code: as written
                text += letter;
            }
            break;
        }
        i = end - 1;
    }
    flush();
    return out;
}

QString headerFooterRunText(const HeaderFooterRun &run, const HeaderFooterContext &context)
{
    using Field = HeaderFooterRun::Field;
    switch (run.field) {
    case Field::None: return run.text;
    case Field::Page: return QString::number(context.page + run.pageOffset);
    case Field::Pages: return QString::number(context.pages);
    case Field::Date:
        // Excel prints the system's short date; M/d/yyyy is the US one.
        return (context.date.isValid() ? context.date : QDate::currentDate()).toString(QStringLiteral("M/d/yyyy"));
    case Field::Time:
        return (context.time.isValid() ? context.time : QTime::currentTime()).toString(QStringLiteral("h:mm AP"));
    case Field::File: return context.fileName.isEmpty() ? QStringLiteral("Book1") : context.fileName;
    case Field::Path: return context.filePath;
    case Field::Sheet: return context.sheetName;
    case Field::Picture: return {}; // the picture lives in the header's VML, not in the code
    }
    return {};
}

QString headerFooterSectionText(const HeaderFooterSection &section, const HeaderFooterContext &context)
{
    QString out;
    for (const HeaderFooterRun &run : section.runs) {
        out += headerFooterRunText(run, context);
    }
    return out;
}

HeaderFooterParts headerFooterParts(const QString &code)
{
    HeaderFooterParts parts;
    QString *section = &parts.center;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c != QLatin1Char('&') || i + 1 >= code.size()) {
            *section += c;
            continue;
        }
        const QChar letter = code.at(i + 1);
        if (letter == QLatin1Char('L') || letter == QLatin1Char('C') || letter == QLatin1Char('R')) {
            section = letter == QLatin1Char('L') ? &parts.left : letter == QLatin1Char('C') ? &parts.center : &parts.right;
            ++i;
            continue;
        }
        if (const QString field = fieldDisplay(letter); !field.isEmpty()) {
            *section += field; // &P+3 reads &[Page]+3: the offset stays text
            ++i;
            continue;
        }
        if (letter == QLatin1Char('&')) {
            *section += QStringLiteral("&&");
            ++i;
            continue;
        }
        const qsizetype end = argumentEnd(code, i + 1); // formatting codes as written
        *section += code.mid(i, end - i);
        i = end - 1;
    }
    return parts;
}

namespace {

// One section in the editing spelling back to codes.
QString sectionCode(const QString &display)
{
    QString out;
    for (qsizetype i = 0; i < display.size(); ++i) {
        const QChar c = display.at(i);
        if (c != QLatin1Char('&') || i + 1 >= display.size()) {
            out += c;
            continue;
        }
        const QChar next = display.at(i + 1);
        if (next == QLatin1Char('&')) {
            out += QStringLiteral("&&");
            ++i;
            continue;
        }
        if (next == QLatin1Char('[')) {
            const qsizetype close = display.indexOf(QLatin1Char(']'), i + 2);
            const QString name = close < 0 ? QString() : display.mid(i + 2, close - i - 2);
            bool matched = false;
            for (const FieldName &field : kFieldNames) {
                if (name.compare(QLatin1String(field.name), Qt::CaseInsensitive) == 0) {
                    out += QLatin1Char('&');
                    out += QLatin1Char(field.code);
                    i = close;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }
        if (next == QLatin1Char('"')) {
            const qsizetype end = argumentEnd(display, i + 1);
            out += display.mid(i, end - i);
            i = end - 1;
            continue;
        }
        out += c; // any other code is written as it stands
    }
    return out;
}

} // namespace

QString headerFooterCode(const HeaderFooterParts &parts)
{
    QString code;
    if (!parts.left.isEmpty()) {
        code += QStringLiteral("&L") + sectionCode(parts.left);
    }
    if (!parts.center.isEmpty()) {
        code += QStringLiteral("&C") + sectionCode(parts.center);
    }
    if (!parts.right.isEmpty()) {
        code += QStringLiteral("&R") + sectionCode(parts.right);
    }
    return code;
}

QList<HeaderFooterPreset> headerFooterPresets(const HeaderFooterContext &context)
{
    // Excel's Header and Footer dropdowns (US English), in Excel's order;
    // each label is how the dropdown shows it for this sheet and file.
    const QString page = QStringLiteral("Page %1").arg(context.page);
    const QString sheet = context.sheetName;
    const QString file = context.fileName.isEmpty() ? QStringLiteral("Book1") : context.fileName;
    const QString path = context.filePath + file;
    const QString date = (context.date.isValid() ? context.date : QDate::currentDate()).toString(QStringLiteral("M/d/yyyy"));
    return {
        {QString(), QStringLiteral("(none)")},
        {QStringLiteral("&CPage &P"), page},
        {QStringLiteral("&CPage &P of &N"), page + QStringLiteral(" of ?")},
        {QStringLiteral("&C&A"), sheet},
        {QStringLiteral("&LConfidential&C&D&RPage &P"), QStringLiteral("Confidential, %1, %2").arg(date, page)},
        {QStringLiteral("&C&F"), file},
        {QStringLiteral("&C&Z&F"), path},
        {QStringLiteral("&C&A&RPage &P"), QStringLiteral("%1, %2").arg(sheet, page)},
        {QStringLiteral("&L&A&CConfidential&RPage &P"), QStringLiteral("%1, Confidential, %2").arg(sheet, page)},
        {QStringLiteral("&C&F&RPage &P"), QStringLiteral("%1, %2").arg(file, page)},
        {QStringLiteral("&C&Z&F&RPage &P"), QStringLiteral("%1, %2").arg(path, page)},
        {QStringLiteral("&CPage &P&R&A"), QStringLiteral("%1, %2").arg(page, sheet)},
        {QStringLiteral("&CPage &P&R&F"), QStringLiteral("%1, %2").arg(page, file)},
        {QStringLiteral("&CPage &P&R&Z&F"), QStringLiteral("%1, %2").arg(page, path)},
    };
}

} // namespace QindaOffice::Calc
