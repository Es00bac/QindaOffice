// SPDX-License-Identifier: GPL-3.0-or-later
// XLSX export of a table or query through the suite's own calc_io writer
// (the same code QindaCalc saves with): one sheet, a bold header row, and
// typed cells (M65) — numbers as numbers, currency in units with a money
// format showing the file's symbol, Yes/No as booleans, dates and
// date-times as Excel date serials with a date format, file fields as
// their description. readXlsxTable (base_xlsx_import.h) reads every one of
// those back as the type it left as.
#include "base_xlsx_export.h"

#include "base_file_field.h"
#include "base_xlsx_import.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <cstdlib>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

qint32 numberStyle(Calc::Workbook &book, const QString &format, int alignment = 0)
{
    Calc::CellStyle style;
    style.numberFormat = format;
    style.alignment = alignment;
    return book.addStyle(style);
}

} // namespace

Engine::Result writeXlsx(const QString &path, const QString &sheetName, const QList<Engine::ColumnInfo> &columns,
                         const QList<QVariantList> &rows, const Engine::CurrencyFormat &currency)
{
    Calc::Workbook book;
    Calc::Sheet &sheet = book.addSheet(sheetName.left(31));
    Calc::CellStyle header;
    header.bold = true;
    const qint32 headerStyle = book.addStyle(header);
    // The symbol as a quoted literal, so any symbol ("kr", "CHF") survives.
    QString symbol = currency.symbol;
    symbol.replace(QLatin1Char('"'), QString());
    const QString digits = currency.decimals > 0 ? QStringLiteral("#,##0.") + QString(currency.decimals, QLatin1Char('0'))
                                                 : QStringLiteral("#,##0");
    const qint32 moneyStyle = numberStyle(book, symbol.isEmpty() ? digits : QStringLiteral("\"%1\"%2").arg(symbol, digits), 2);
    const qint32 dateStyle = numberStyle(book, QStringLiteral("yyyy-mm-dd"));
    const qint32 dateTimeStyle = numberStyle(book, QStringLiteral("yyyy-mm-dd hh:mm:ss"));
    double scale = 1;
    for (int i = 0; i < currency.decimals; ++i) {
        scale *= 10;
    }
    book.beginBulkLoad();
    for (int c = 0; c < columns.size(); ++c) {
        const Calc::CellAddr addr{0, c};
        book.setCellValue(0, addr, Calc::Value(columns.at(c).label()));
        sheet.setStyleIdAt(addr, headerStyle);
        const Engine::FieldType type = columns.at(c).type;
        sheet.setColumnWidth(c, type == Engine::FieldType::LongText ? 260
                                : type == Engine::FieldType::DateTime ? 150
                                                                       : 120);
    }
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < columns.size(); ++c) {
            const QVariant v = rows.at(r).value(c);
            if (!v.isValid() || v.isNull()) {
                continue;
            }
            const Calc::CellAddr addr{r + 1, c};
            const Engine::FieldType type = columns.at(c).type;
            const int id = v.metaType().id();
            const bool numeric = id == QMetaType::LongLong || id == QMetaType::Double;
            if (type == Engine::FieldType::Currency && id == QMetaType::LongLong) {
                book.setCellValue(0, addr, Calc::Value(double(v.toLongLong()) / scale));
                sheet.setStyleIdAt(addr, moneyStyle);
            } else if (type == Engine::FieldType::YesNo && numeric) {
                book.setCellValue(0, addr, Calc::Value(v.toLongLong() != 0));
            } else if (type == Engine::FieldType::Date || type == Engine::FieldType::DateTime) {
                // Stored as ISO text (base_types.h); anything else stays text.
                const QString text = v.toString();
                const QDateTime when = type == Engine::FieldType::Date
                                           ? QDateTime(QDate::fromString(text, Qt::ISODate), QTime(0, 0))
                                           : QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                if (when.isValid()) {
                    book.setCellValue(0, addr, Calc::Value(serialFromDateTime(when)));
                    sheet.setStyleIdAt(addr, type == Engine::FieldType::Date ? dateStyle : dateTimeStyle);
                } else {
                    book.setCellValue(0, addr, Calc::Value(text));
                }
            } else if (type == Engine::FieldType::File || type == Engine::FieldType::Hyperlink) {
                // M86: a hyperlink exports the text it shows, as Access does.
                book.setCellValue(0, addr, Calc::Value(Engine::displayText(type, v, currency)));
            } else if (type == Engine::FieldType::LargeNumber && id == QMetaType::LongLong
                       && std::llabs(v.toLongLong()) > (qint64(1) << 53)) {
                // M86: past 2^53 a double would change the number; it goes as text.
                book.setCellValue(0, addr, Calc::Value(QString::number(v.toLongLong())));
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
