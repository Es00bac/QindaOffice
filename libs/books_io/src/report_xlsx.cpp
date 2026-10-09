// SPDX-License-Identifier: GPL-3.0-or-later
// A ReportTable as an xlsx workbook through calc_io (report_xlsx.h).
#include "report_xlsx.h"

#include "money.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <QRegularExpression>

namespace QindaOffice::Books {

namespace {

namespace Calc = ::QindaOffice::Calc;

// Excel's 1900 date system: serial 0 is 1899-12-30.
double dateSerial(const QDate &date)
{
    return double(QDate(1899, 12, 30).daysTo(date));
}

double scaled(qint64 value, int decimals)
{
    double d = double(value);
    for (int i = 0; i < decimals; ++i) {
        d /= 10.0;
    }
    return d;
}

QString digits(int decimals)
{
    return decimals > 0 ? QStringLiteral(".") + QString(decimals, QLatin1Char('0')) : QString();
}

QString sheetName(const QString &title)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\[\\]:*?/\\\\]"));
    QString name = title;
    name.remove(forbidden);
    name = name.trimmed().left(31);
    return name.isEmpty() ? QStringLiteral("Report") : name;
}

struct Styles {
    qint32 company = 0;
    qint32 title = 0;
    qint32 header = 0;
    qint32 headerNumber = 0;
    qint32 text = 0;
    qint32 strong = 0; // section, subtotal and total labels
    qint32 money = 0;
    qint32 moneyTotal = 0;
    qint32 date = 0;
    qint32 note = 0;
    QHash<QString, qint32> numbers; // number format → style (Number and Percent cells)
    QHash<QString, qint32> numbersTotal;
};

constexpr int kTop = 1 << 2;
constexpr int kBottom = 1 << 3;

} // namespace

QString ReportXlsx::moneyFormat(const QString &currency)
{
    const CurrencyInfo info = currencyInfo(currency);
    QString symbol = info.symbol.isEmpty() ? currency : info.symbol;
    symbol.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    const QString body = QStringLiteral("#,##0") + digits(info.exponent);
    return QStringLiteral("\"%1\"%2;-\"%1\"%2").arg(symbol, body);
}

bool ReportXlsx::write(const ReportTable &table, const QString &path, QString *error)
{
    Calc::Workbook book;
    Calc::Sheet &sheet = book.addSheet(sheetName(table.title));
    const int exponent = currencyInfo(table.currency).exponent;
    Styles s;
    const auto style = [&book](const auto &configure) {
        Calc::CellStyle st;
        configure(st);
        return book.addStyle(st);
    };
    s.company = style([](Calc::CellStyle &st) { st.bold = true; });
    s.title = style([](Calc::CellStyle &st) { st.bold = true; st.fontSize = 14; });
    s.header = style([](Calc::CellStyle &st) { st.bold = true; st.borders = kBottom; });
    s.headerNumber = style([](Calc::CellStyle &st) { st.bold = true; st.borders = kBottom; st.alignment = 2; });
    s.text = style([](Calc::CellStyle &) {});
    s.strong = style([](Calc::CellStyle &st) { st.bold = true; });
    s.money = style([&](Calc::CellStyle &st) { st.numberFormat = moneyFormat(table.currency); st.alignment = 2; });
    s.moneyTotal = style([&](Calc::CellStyle &st) {
        st.numberFormat = moneyFormat(table.currency);
        st.alignment = 2;
        st.bold = true;
        st.borders = kTop;
    });
    s.date = style([](Calc::CellStyle &st) { st.numberFormat = QStringLiteral("yyyy-mm-dd"); });
    s.note = style([](Calc::CellStyle &st) { st.italic = true; });
    const auto numberStyle = [&](const QString &format, bool total) {
        QHash<QString, qint32> &cache = total ? s.numbersTotal : s.numbers;
        if (!cache.contains(format)) {
            cache.insert(format, style([&](Calc::CellStyle &st) {
                             st.numberFormat = format;
                             st.alignment = 2;
                             st.bold = total;
                             st.borders = total ? kTop : 0;
                         }));
        }
        return cache.value(format);
    };

    book.beginBulkLoad();
    const auto put = [&](qint32 row, qint32 col, const Calc::Value &value, qint32 styleId) {
        const Calc::CellAddr addr{row, col};
        book.setCellValue(0, addr, value);
        sheet.setStyleIdAt(addr, styleId);
    };
    put(0, 0, Calc::Value(table.company), s.company);
    put(1, 0, Calc::Value(table.title), s.title);
    put(2, 0, Calc::Value(table.subtitle), s.text);
    const qint32 headerRow = 4;
    for (qint32 c = 0; c < table.columns.size(); ++c) {
        const ReportColumn &column = table.columns.at(c);
        put(headerRow, c, Calc::Value(column.title), column.type == ColumnType::Text ? s.header : s.headerNumber);
        sheet.setColumnWidth(c, c == 0 ? 300 : column.type == ColumnType::Text ? 200 : 120);
    }
    qint32 row = headerRow + 1;
    for (const ReportRow &r : table.rows) {
        if (r.kind == RowKind::Spacer) {
            ++row;
            continue;
        }
        const bool total = r.kind == RowKind::Subtotal || r.kind == RowKind::Total;
        const bool strong = total || r.kind == RowKind::Section;
        for (qint32 c = 0; c < r.cells.size() && c < table.columns.size(); ++c) {
            const ReportCell &cell = r.cells.at(c);
            if (cell.empty) {
                continue;
            }
            switch (cell.type) {
            case ColumnType::Money:
                put(row, c, Calc::Value(scaled(cell.value, exponent)), total ? s.moneyTotal : s.money);
                break;
            case ColumnType::Number:
                put(row, c, Calc::Value(scaled(cell.value, cell.scale)),
                    numberStyle(QStringLiteral("#,##0") + digits(cell.scale), total));
                break;
            case ColumnType::Percent:
                // 12.5% is value 125 at scale 1; the cell holds 0.125.
                put(row, c, Calc::Value(scaled(cell.value, cell.scale + 2)),
                    numberStyle(QStringLiteral("0") + digits(cell.scale) + QLatin1Char('%'), total));
                break;
            case ColumnType::Date:
                put(row, c, Calc::Value(dateSerial(cell.date)), s.date);
                break;
            case ColumnType::Duration:
                // M68: minutes as a fraction of a day under [h]:mm — a
                // duration Excel sums and shows as 6:05, never text.
                put(row, c, Calc::Value(double(cell.value) / (24.0 * 60.0)), numberStyle(QStringLiteral("[h]:mm"), total));
                break;
            case ColumnType::Text: {
                // Indentation as the screen and the PDF show it.
                const QString indent = c == 0 ? QString(r.depth * 3, QLatin1Char(' ')) : QString();
                put(row, c, Calc::Value(indent + cell.text), strong && c == 0 ? s.strong : s.text);
                break;
            }
            }
        }
        ++row;
    }
    if (!table.notes.isEmpty()) {
        ++row;
        for (const QString &note : table.notes) {
            put(row++, 0, Calc::Value(note), s.note);
        }
    }
    book.endBulkLoad();
    sheet.setFreeze(headerRow + 1, 1);
    QString failure;
    if (!Calc::IO::writeXlsxFile(book, path, &failure)) {
        if (error != nullptr) {
            *error = failure.isEmpty() ? QStringLiteral("The workbook could not be written.") : failure;
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Books
