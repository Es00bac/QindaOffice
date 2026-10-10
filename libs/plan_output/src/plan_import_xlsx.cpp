// SPDX-License-Identifier: GPL-3.0-or-later
// An Excel worksheet as an import table (plan_import_xlsx.h).
#include "plan_import_xlsx.h"

#include "plan_time.h"

#include <number_format.h>
#include <workbook.h>
#include <xlsx_bridge.h>

#include <cmath>

namespace QindaOffice::Plan {

namespace {

// A number format that shows a date or a time (quoted text and [..]
// sections aside): it has a year, day, hour or clock token.
bool isDateFormat(const QString &code)
{
    QString plain;
    bool quoted = false;
    int bracket = 0;
    for (const QChar c : code.toLower()) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && c == QLatin1Char('[')) {
            ++bracket;
        } else if (!quoted && c == QLatin1Char(']')) {
            bracket = std::max(0, bracket - 1);
        } else if (!quoted && bracket == 0) {
            plain.append(c);
        }
    }
    return plain != QLatin1String("general")
        && (plain.contains(QLatin1Char('y')) || plain.contains(QLatin1Char('d')) || plain.contains(QLatin1Char('h'))
            || plain.contains(QLatin1Char(':')));
}

QString shortest(double value)
{
    QString s = QString::number(value, 'f', 6);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('.'))) {
            s.chop(1);
        }
    }
    return s;
}

QString cellText(const Calc::Workbook &book, const Calc::Cell &cell)
{
    const Calc::Value &v = cell.value;
    if (std::holds_alternative<QString>(v)) {
        return std::get<QString>(v);
    }
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v) ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    if (!std::holds_alternative<double>(v)) {
        return {}; // empty or an error value
    }
    const double number = std::get<double>(v);
    const Calc::CellStyle *style = book.style(cell.styleId);
    const QString code = style != nullptr ? style->numberFormat : QStringLiteral("General");
    if (isDateFormat(code)) {
        // Excel's serial days since 30 Dec 1899; 25569 is 1 Jan 1970.
        return toIso(Minutes(std::llround((number - 25569.0) * 1440.0)));
    }
    if (code.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0 || code.isEmpty()) {
        return shortest(number);
    }
    return Calc::NumberFormat(code).formatNumber(number).text;
}

} // namespace

bool readTaskTableXlsx(const QString &path, ImportTable *out, QString *error)
{
    Calc::Workbook book;
    QString why;
    if (!Calc::IO::readXlsxFile(path, &book, &why)) {
        if (error != nullptr) {
            *error = why.isEmpty() ? QStringLiteral("The workbook could not be read.") : why;
        }
        return false;
    }
    if (book.sheetCount() == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("The workbook has no worksheet.");
        }
        return false;
    }
    const Calc::Sheet &sheet = book.sheetAt(0);
    int rows = 0;
    int columns = 0;
    for (auto it = sheet.cells().cbegin(); it != sheet.cells().cend(); ++it) {
        rows = std::max(rows, int(it.key() & 0xffffffffu) + 1);
        columns = std::max(columns, int(it.key() >> 32) + 1);
    }
    rows = std::min(rows, 100000); // a bounded table, as an import is
    columns = std::min(columns, 256);
    out->rows = QList<QStringList>(rows, QStringList(columns));
    for (auto it = sheet.cells().cbegin(); it != sheet.cells().cend(); ++it) {
        const int row = int(it.key() & 0xffffffffu);
        const int column = int(it.key() >> 32);
        if (row < rows && column < columns) {
            out->rows[row][column] = cellText(book, it.value());
        }
    }
    return true;
}

} // namespace QindaOffice::Plan
