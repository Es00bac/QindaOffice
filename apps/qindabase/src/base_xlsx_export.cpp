// SPDX-License-Identifier: GPL-3.0-or-later
// XLSX export of a table or query through the suite's own calc_io writer
// (the same code QindaCalc saves with): one sheet, a bold header row,
// numbers as numbers (currency in units with a money format), Yes/No as
// booleans, dates and text as text.
#include "base_xlsx_export.h"

#include <workbook.h>
#include <xlsx_bridge.h>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

Engine::Result writeXlsx(const QString &path, const QString &sheetName, const QList<Engine::ColumnInfo> &columns,
                         const QList<QVariantList> &rows, const Engine::CurrencyFormat &currency)
{
    Calc::Workbook book;
    Calc::Sheet &sheet = book.addSheet(sheetName.left(31));
    Calc::CellStyle header;
    header.bold = true;
    const qint32 headerStyle = book.addStyle(header);
    Calc::CellStyle money;
    money.numberFormat = currency.decimals > 0
                             ? QStringLiteral("#,##0.") + QString(currency.decimals, QLatin1Char('0'))
                             : QStringLiteral("#,##0");
    money.alignment = 2;
    const qint32 moneyStyle = book.addStyle(money);
    double scale = 1;
    for (int i = 0; i < currency.decimals; ++i) {
        scale *= 10;
    }
    book.beginBulkLoad();
    for (int c = 0; c < columns.size(); ++c) {
        const Calc::CellAddr addr{0, c};
        book.setCellValue(0, addr, Calc::Value(columns.at(c).label()));
        sheet.setStyleIdAt(addr, headerStyle);
        sheet.setColumnWidth(c, columns.at(c).type == Engine::FieldType::LongText ? 260 : 120);
    }
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < columns.size(); ++c) {
            const QVariant v = rows.at(r).value(c);
            if (!v.isValid()) {
                continue;
            }
            const Calc::CellAddr addr{r + 1, c};
            const Engine::FieldType type = columns.at(c).type;
            const bool numeric = v.metaType().id() == QMetaType::LongLong || v.metaType().id() == QMetaType::Double;
            if (type == Engine::FieldType::Currency && v.metaType().id() == QMetaType::LongLong) {
                book.setCellValue(0, addr, Calc::Value(double(v.toLongLong()) / scale));
                sheet.setStyleIdAt(addr, moneyStyle);
            } else if (type == Engine::FieldType::YesNo && numeric) {
                book.setCellValue(0, addr, Calc::Value(v.toLongLong() != 0));
            } else if (numeric) {
                book.setCellValue(0, addr, Calc::Value(v.toDouble()));
            } else {
                book.setCellValue(0, addr, Calc::Value(v.toString()));
            }
        }
    }
    book.endBulkLoad();
    sheet.setFreeze(1, 0);
    QString error;
    if (!Calc::IO::writeXlsxFile(book, path, &error)) {
        return Engine::Result::failure(error.isEmpty() ? QStringLiteral("The workbook could not be written.") : error);
    }
    return Engine::Result::success();
}

} // namespace QindaOffice::Apps::Base
