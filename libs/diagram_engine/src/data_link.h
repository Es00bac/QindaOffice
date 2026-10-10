// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// External data (M87, D-095): Visio's Link Data to Shapes. A table read
// from an Excel workbook (through calc_io, the reader QindaCalc opens
// workbooks with: cached formula values, numbers and dates as Excel shows
// them) or a CSV file becomes a DataRecordset of the drawing; its rows
// link to shapes, whose shape data then carries the row's values, and
// Refresh reads the file again and updates every linked shape.
//
// AGENT-CONTRACT (the linked fields): a linked shape holds one data field
// per column of its recordset, keyed by the column's name, in column
// order; a refresh rewrites exactly those fields and leaves the shape's
// other fields alone. Unlinking keeps the values (Visio does too).

#include "drawing.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaOffice::Diagram {

struct DataTable {
    QStringList columns;
    QVector<QStringList> rows;   // a cell per column
    QStringList sheets;          // a workbook's sheets, in order
    QString sheet;               // the one read
};

// The first row names the columns (blank names become "Column n"); wholly
// empty rows are skipped. A workbook's `sheet` ("" = the first holding
// data); .xls and other formats are refused with a message.
bool readDataTable(const QString &path, const QString &sheet, DataTable *out, QString *error = nullptr);
[[nodiscard]] DataTable dataTableFromCsv(const QString &text);

namespace Ops {

// A recordset of `table` named `name` (made unique), read from `source`;
// `keyColumn` identifies rows (-1: their position). Its id.
QString addRecordset(Drawing &drawing, const DataTable &table, const QString &name, const QString &source,
                     int keyColumn, const QDateTime &when);
// Replaces the recordset's rows with `table` (its key column kept by name)
// and rewrites every linked shape on every page; `missing` counts linked
// shapes whose row is gone (their values stay).
bool refreshRecordset(Drawing &drawing, const QString &recordsetId, const DataTable &table, const QDateTime &when,
                      int *missing = nullptr);
// Removes the recordset; its shapes keep their values, unlinked.
bool removeRecordset(Drawing &drawing, const QString &recordsetId);
// Links shapes to row `row` of the recordset and writes its values.
bool linkShapes(const Drawing &drawing, Page &page, const QStringList &shapeIds, const QString &recordsetId, int row);
// Visio's Automatically Link: each shape whose text (`shapeField` empty) or
// data field `shapeField` equals a row's value in `column` (trimmed, case
// blind) links to that row. Among `ids`, or every shape when empty.
// Returns how many shapes were linked.
int autoLink(const Drawing &drawing, Page &page, const QString &recordsetId, int column, const QString &shapeField,
             const QStringList &ids = {});
bool unlinkShapes(Page &page, const QStringList &ids);
// A linked shape of `stencilId` for each row (Visio: drag rows onto the
// page), in a grid from `origin`, its text the row's `textColumn` value.
QStringList shapesFromRows(Drawing &drawing, Page &page, const QString &recordsetId, const QList<int> &rows,
                           const QString &stencilId, QPointF origin, int textColumn);

} // namespace Ops

} // namespace QindaOffice::Diagram
