// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_clipboard.h"

#include "spreadsheet_document.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace QindaOffice::Apps::QindaCalc {

namespace {

const char kRichMime[] = "application/x-qindacalc-cells";

// Input text (formula or literal source) per cell; empty string for
// blank cells so row/column geometry survives the round trip. Carried only
// in the rich in-app mime — external apps get DISPLAY text (the header
// contract and D-006: pasting into an editor must not produce "=A1*2").
QStringList rangeInputs(const SpreadsheetDocument *document, const CalcEng::CellRange &range)
{
    QStringList rows;
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        QStringList cells;
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            cells.append(document->inputTextAt({row, col}));
        }
        rows.append(cells.join(QLatin1Char('\t')));
    }
    return rows;
}

QStringList rangeDisplayText(const SpreadsheetDocument *document, const CalcEng::CellRange &range)
{
    QStringList rows;
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        QStringList cells;
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            cells.append(document->displayTextAt(document->activeSheet(), {row, col}));
        }
        rows.append(cells.join(QLatin1Char('\t')));
    }
    return rows;
}

} // namespace

const char *richCellsMimeType()
{
    return kRichMime;
}

void copyRangeToClipboard(SpreadsheetDocument *document, const CalcEng::CellRange &range)
{
    if (!range.isValid()) {
        return;
    }
    const QStringList rows = rangeInputs(document, range);
    auto *mime = new QMimeData;
    mime->setText(rangeDisplayText(document, range).join(QLatin1Char('\n')));
    QJsonObject root;
    root.insert(QStringLiteral("v"), 1);
    QJsonArray jsonRows;
    for (const QString &line : rows) {
        jsonRows.append(QJsonArray::fromStringList(line.split(QLatin1Char('\t'))));
    }
    root.insert(QStringLiteral("rows"), jsonRows);
    mime->setData(QString::fromLatin1(kRichMime),
                  QJsonDocument(root).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void cutRangeToClipboard(SpreadsheetDocument *document, const CalcEng::CellRange &range)
{
    copyRangeToClipboard(document, range);
    document->clearCells(range);
}

bool pasteClipboardAt(SpreadsheetDocument *document, const CalcEng::CellAddr &at)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr) {
        return false;
    }
    QStringList rows;
    if (mime->hasFormat(QString::fromLatin1(kRichMime))) {
        const QJsonDocument doc =
            QJsonDocument::fromJson(mime->data(QString::fromLatin1(kRichMime)));
        const QJsonArray jsonRows = doc.object().value(QStringLiteral("rows")).toArray();
        for (const QJsonValue &row : jsonRows) {
            QStringList cells;
            for (const QJsonValue &cell : row.toArray()) {
                cells.append(cell.toString());
            }
            rows.append(cells.join(QLatin1Char('\t')));
        }
    } else if (mime->hasText()) {
        rows = mime->text().split(QLatin1Char('\n'));
        // A trailing newline from external TSV must not paste a blank row.
        while (!rows.isEmpty() && rows.last().isEmpty()) {
            rows.removeLast();
        }
        // CRLF sources leave a trailing \r inside every row's last cell.
        for (QString &row : rows) {
            if (row.endsWith(QLatin1Char('\r'))) {
                row.chop(1);
            }
        }
    } else {
        return false;
    }
    document->pasteCells(at, rows);
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
