// SPDX-License-Identifier: GPL-3.0-or-later
// One sheet of an .xlsx workbook → a typed CsvTable (base_xlsx_import.h).
#include "base_xlsx_import.h"

#include <number_format.h>
#include <workbook.h>
#include <xlsx_bridge.h>

#include <QFileInfo>
#include <QRegularExpression>

#include <climits>
#include <cmath>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

constexpr qint64 kMaxBytes = qint64(200) * 1024 * 1024;
constexpr int kMaxColumns = 1024;

// A format code without its quoted literals, escaped characters, padding
// (_x) and fill (*x) — what is left are the tokens.
QString tokensOf(const QString &code)
{
    QString out;
    bool quoted = false;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const QChar ch = code.at(i);
        if (quoted) {
            quoted = ch != QLatin1Char('"');
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted = true;
        } else if (ch == QLatin1Char('\\') || ch == QLatin1Char('_') || ch == QLatin1Char('*')) {
            ++i;
        } else {
            out += ch;
        }
    }
    return out;
}

const QRegularExpression &elapsed()
{
    static const QRegularExpression re(QStringLiteral("\\[(?:h+|m+|s+)\\]"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString withoutTags(QString tokens)
{
    static const QRegularExpression tags(QStringLiteral("\\[[^\\]]*\\]"));
    static const QRegularExpression general(QStringLiteral("general"), QRegularExpression::CaseInsensitiveOption);
    tokens.remove(tags);
    tokens.remove(general);
    return tokens;
}

struct Typed {
    enum Kind { Blank, Number, Money, Date, DateTime, Bool, Text } kind = Blank;
    double number = 0;
    bool boolean = false;
    QString text;    // a text cell's text
    QString display; // how Excel shows it (numbers in their format)
};

Typed typed(const Calc::Value &value, const QString &code, const Engine::CurrencyFormat &currency)
{
    Typed t;
    if (const double *d = std::get_if<double>(&value)) {
        t.number = *d;
        t.kind = isDateFormatCode(code) ? (hasTimeCode(code) || std::floor(*d) != *d ? Typed::DateTime : Typed::Date)
                 : isCurrencyFormatCode(code, currency.symbol) ? Typed::Money
                                                               : Typed::Number;
        t.display = Calc::formatWithCode(value, code).text;
    } else if (const bool *b = std::get_if<bool>(&value)) {
        t.kind = Typed::Bool;
        t.boolean = *b;
        t.display = *b ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    } else if (const QString *s = std::get_if<QString>(&value); s != nullptr && !s->isEmpty()) {
        t.kind = Typed::Text;
        t.text = *s;
        t.display = *s;
    } // blanks and errors (#N/A …) import as empty
    return t;
}

bool fits(double value, int decimals)
{
    const double scaled = value * std::pow(10.0, decimals);
    return std::fabs(scaled - std::round(scaled)) <= 1e-9 * std::max(1.0, std::fabs(scaled));
}

QString numberText(double value)
{
    QString text = QString::number(value, 'g', 15);
    if (text.toDouble() != value) {
        text = QString::number(value, 'g', 17); // the shortest text that reads back exactly
    }
    return text;
}

// The type the cells state, or nullopt when text decides (base_csv rules).
std::optional<Engine::FieldType> stated(const QList<Typed> &cells, const Engine::CurrencyFormat &currency)
{
    bool any = false;
    bool allBool = true;
    bool allNumeric = true;
    bool allDates = true;
    bool anyDate = false;
    bool anyTime = false;
    bool anyMoney = false;
    bool integral = true;
    bool moneyFits = true;
    for (const Typed &t : cells) {
        if (t.kind == Typed::Blank) {
            continue;
        }
        if (t.kind == Typed::Text) {
            return std::nullopt;
        }
        any = true;
        const bool date = t.kind == Typed::Date || t.kind == Typed::DateTime;
        allBool = allBool && t.kind == Typed::Bool;
        allNumeric = allNumeric && t.kind != Typed::Bool;
        allDates = allDates && date;
        anyDate = anyDate || date;
        anyTime = anyTime || t.kind == Typed::DateTime;
        anyMoney = anyMoney || t.kind == Typed::Money;
        integral = integral && std::floor(t.number) == t.number && std::fabs(t.number) < 1e15;
        moneyFits = moneyFits && fits(t.number, currency.decimals);
    }
    if (!any) {
        return std::nullopt;
    }
    if (allBool) {
        return Engine::FieldType::YesNo;
    }
    if (!allNumeric || (anyDate && !allDates)) {
        return Engine::FieldType::Text; // mixed: shown as Excel shows them
    }
    if (allDates) {
        return anyTime ? Engine::FieldType::DateTime : Engine::FieldType::Date;
    }
    if (anyMoney && moneyFits) {
        return Engine::FieldType::Currency;
    }
    return integral && !anyMoney ? Engine::FieldType::Integer : Engine::FieldType::Number;
}

// A cell as the canonical text the stated type parses (base_csv.h).
QString canonical(const Typed &t, std::optional<Engine::FieldType> type, const Engine::CurrencyFormat &currency)
{
    if (t.kind == Typed::Blank) {
        return {};
    }
    if (!type) {
        return t.kind == Typed::Text ? t.text : t.display;
    }
    switch (*type) {
    case Engine::FieldType::YesNo:
        return t.boolean ? QStringLiteral("Yes") : QStringLiteral("No");
    case Engine::FieldType::Date:
        return dateTimeFromSerial(t.number).date().toString(Qt::ISODate);
    case Engine::FieldType::DateTime:
        return dateTimeFromSerial(t.number).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case Engine::FieldType::Currency:
        return QString::number(t.number, 'f', currency.decimals);
    case Engine::FieldType::Integer:
        return QString::number(qint64(t.number));
    case Engine::FieldType::Number:
        return numberText(t.number);
    default:
        return t.display;
    }
}

} // namespace

bool isDateFormatCode(const QString &code)
{
    const QString tokens = tokensOf(code);
    if (elapsed().match(tokens).hasMatch()) {
        return true;
    }
    static const QRegularExpression date(QStringLiteral("[ydhms]|AM/PM|A/P"), QRegularExpression::CaseInsensitiveOption);
    return date.match(withoutTags(tokens)).hasMatch();
}

bool hasTimeCode(const QString &code)
{
    const QString tokens = tokensOf(code);
    if (elapsed().match(tokens).hasMatch()) {
        return true;
    }
    static const QRegularExpression time(QStringLiteral("[hs]|AM/PM|A/P"), QRegularExpression::CaseInsensitiveOption);
    return time.match(withoutTags(tokens)).hasMatch();
}

bool isCurrencyFormatCode(const QString &code, const QString &symbol)
{
    QString c = code;
    static const QRegularExpression locale(QStringLiteral("\\[\\$-[0-9A-Fa-f]+\\]")); // [$-409]: a locale, no money
    c.remove(locale);
    static const QRegularExpression signs(QStringLiteral("[$€£¥₹₩₽¤]|\\[\\$[^\\]-]"));
    if (signs.match(c).hasMatch()) {
        return true;
    }
    return !symbol.trimmed().isEmpty() && c.contains(symbol.trimmed());
}

QDateTime dateTimeFromSerial(double serial)
{
    const double days = std::floor(serial);
    qint64 msecs = qRound64((serial - days) * 86400000.0);
    QDate date = QDate(1899, 12, 30).addDays(qint64(days));
    if (msecs >= 86400000) {
        msecs = 0;
        date = date.addDays(1);
    }
    // Seconds are what QindaBase keeps; round, don't truncate.
    const qint64 seconds = (msecs + 500) / 1000;
    return QDateTime(date, QTime(0, 0).addSecs(int(std::min<qint64>(seconds, 86399))));
}

double serialFromDateTime(const QDateTime &when)
{
    return double(QDate(1899, 12, 30).daysTo(when.date())) + when.time().msecsSinceStartOfDay() / 86400000.0;
}

std::optional<XlsxTable> readXlsxTable(const QString &path, const QString &sheet, bool headerRow,
                                       const Engine::CurrencyFormat &currency, QString *error)
{
    const QFileInfo info(path);
    if (info.size() > kMaxBytes) {
        *error = QObject::tr("The file is larger than 200 MB.");
        return std::nullopt;
    }
    if (info.suffix().compare(QLatin1String("xls"), Qt::CaseInsensitive) == 0) {
        *error = QObject::tr("Excel 97–2003 workbooks (.xls) cannot be read; save it as .xlsx first.");
        return std::nullopt;
    }
    Calc::Workbook book;
    QString readError;
    if (!Calc::IO::readXlsxFile(path, &book, &readError)) {
        *error = readError.isEmpty() ? QObject::tr("The workbook could not be read.") : readError;
        return std::nullopt;
    }
    XlsxTable out;
    int chosen = -1;
    for (qint32 i = 0; i < book.sheetCount(); ++i) {
        const Calc::Sheet &s = book.sheetAt(i);
        out.sheets.append(s.name());
        if ((sheet.isEmpty() && chosen < 0 && !s.cells().isEmpty()) || s.name() == sheet) {
            chosen = i;
        }
    }
    if (chosen < 0) {
        *error = sheet.isEmpty() ? QObject::tr("The workbook has no data.") : QObject::tr("There is no sheet %1.").arg(sheet);
        return std::nullopt;
    }
    const Calc::Sheet &s = book.sheetAt(chosen);
    out.sheet = s.name();
    // The used range: rows and columns holding a value.
    qint32 top = INT_MAX;
    qint32 bottom = -1;
    qint32 left = INT_MAX;
    qint32 right = -1;
    for (auto it = s.cells().cbegin(); it != s.cells().cend(); ++it) {
        if (Calc::isBlank(it->value) || (Calc::isText(it->value) && std::get<QString>(it->value).isEmpty())) {
            continue;
        }
        const auto row = qint32(it.key() & 0xFFFFFFFFu);
        const auto col = qint32(it.key() >> 32);
        top = std::min(top, row);
        bottom = std::max(bottom, row);
        left = std::min(left, col);
        right = std::max(right, col);
    }
    if (bottom < 0) {
        *error = QObject::tr("The sheet %1 is empty.").arg(s.name());
        return std::nullopt;
    }
    right = std::min(right, left + kMaxColumns - 1);
    const int width = right - left + 1;
    QList<QList<Typed>> rows;
    for (qint32 r = top; r <= bottom; ++r) {
        QList<Typed> cells;
        bool blank = true;
        for (qint32 c = left; c <= right; ++c) {
            const Calc::CellAddr addr{r, c};
            const Calc::Cell *cell = s.cellAt(addr);
            const Calc::CellStyle *style = book.style(s.styleIdAt(addr));
            const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat
                                                                                     : QStringLiteral("General");
            cells.append(cell != nullptr ? typed(cell->value, code, currency) : Typed());
            blank = blank && cells.last().kind == Typed::Blank;
        }
        if (!blank) {
            rows.append(cells);
        }
    }
    Engine::CsvTable &table = out.table;
    table.headerRow = headerRow;
    if (headerRow && !rows.isEmpty()) {
        const QList<Typed> head = rows.takeFirst();
        for (int c = 0; c < width; ++c) {
            const QString name = head.at(c).display.trimmed();
            table.header.append(name.isEmpty() ? QStringLiteral("Field%1").arg(c + 1) : name);
        }
    } else {
        for (int c = 0; c < width; ++c) {
            table.header.append(QStringLiteral("Field%1").arg(c + 1));
        }
    }
    for (int c = 0; c < width; ++c) {
        QList<Typed> column;
        column.reserve(rows.size());
        for (const QList<Typed> &row : std::as_const(rows)) {
            column.append(row.at(c));
        }
        table.types.append(stated(column, currency));
    }
    for (const QList<Typed> &row : std::as_const(rows)) {
        QStringList texts;
        for (int c = 0; c < width; ++c) {
            texts.append(canonical(row.at(c), table.types.at(c), currency));
        }
        table.rows.append(texts);
    }
    return out;
}

} // namespace QindaOffice::Apps::Base
