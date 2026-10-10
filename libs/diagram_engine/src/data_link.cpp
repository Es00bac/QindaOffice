// SPDX-License-Identifier: GPL-3.0-or-later
// External data: reading tables and linking their rows to shapes
// (data_link.h).
#include "data_link.h"

#include "geometry.h"
#include "importers.h"
#include "ops.h"
#include "stencils.h"

#include <number_format.h>
#include <workbook.h>
#include <xlsx_bridge.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DiagramData", text);
}

constexpr qint64 kMaxBytes = qint64(64) * 1024 * 1024;
constexpr int kMaxColumns = 256;
constexpr int kMaxRows = 20000;

// Header row → unique, non-blank column names.
QStringList columnNames(const QStringList &header)
{
    QStringList out;
    QSet<QString> taken;
    for (int i = 0; i < header.size(); ++i) {
        QString name = header.at(i).trimmed();
        if (name.isEmpty()) {
            name = tr("Column %1").arg(i + 1);
        }
        QString unique = name;
        for (int n = 2; taken.contains(unique.toLower()); ++n) {
            unique = QStringLiteral("%1 (%2)").arg(name).arg(n);
        }
        taken.insert(unique.toLower());
        out.append(unique);
    }
    return out;
}

DataTable tableOf(QVector<QStringList> rows)
{
    DataTable t;
    rows.removeIf([](const QStringList &row) {
        return std::all_of(row.cbegin(), row.cend(), [](const QString &c) { return c.trimmed().isEmpty(); });
    });
    if (rows.isEmpty()) {
        return t;
    }
    t.columns = columnNames(rows.takeFirst());
    for (QStringList row : std::as_const(rows)) {
        while (row.size() < t.columns.size()) {
            row.append(QString());
        }
        t.rows.append(row.mid(0, t.columns.size()));
        if (t.rows.size() >= kMaxRows) {
            break;
        }
    }
    return t;
}

bool readWorkbook(const QString &path, const QString &sheet, DataTable *out, QString *error)
{
    Calc::Workbook book;
    QString readError;
    if (!Calc::IO::readXlsxFile(path, &book, &readError)) {
        *error = readError.isEmpty() ? tr("The workbook could not be read.") : readError;
        return false;
    }
    int chosen = -1;
    QStringList sheets;
    for (qint32 i = 0; i < book.sheetCount(); ++i) {
        const Calc::Sheet &s = book.sheetAt(i);
        sheets.append(s.name());
        if ((sheet.isEmpty() && chosen < 0 && !s.cells().isEmpty()) || s.name() == sheet) {
            chosen = i;
        }
    }
    if (chosen < 0) {
        *error = sheet.isEmpty() ? tr("The workbook has no data.") : tr("The workbook has no sheet %1.").arg(sheet);
        return false;
    }
    const Calc::Sheet &s = book.sheetAt(chosen);
    // The used range: the rows and columns that hold a value.
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
        *error = tr("The sheet %1 is empty.").arg(s.name());
        return false;
    }
    right = std::min(right, left + kMaxColumns - 1);
    bottom = std::min(bottom, top + kMaxRows);
    QVector<QStringList> rows;
    for (qint32 r = top; r <= bottom; ++r) {
        QStringList cells;
        for (qint32 c = left; c <= right; ++c) {
            const Calc::CellAddr addr{r, c};
            const Calc::Cell *cell = s.cellAt(addr);
            const Calc::CellStyle *style = book.style(s.styleIdAt(addr));
            const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat
                                                                                     : QStringLiteral("General");
            // As Excel shows it: numbers and dates in their format; errors blank.
            QString text;
            if (cell != nullptr && !Calc::isBlank(cell->value) && !std::holds_alternative<Calc::Err>(cell->value)) {
                text = Calc::isText(cell->value) ? std::get<QString>(cell->value) : Calc::formatWithCode(cell->value, code).text;
            }
            cells.append(text);
        }
        rows.append(cells);
    }
    *out = tableOf(rows);
    out->sheets = sheets;
    out->sheet = s.name();
    return true;
}

// Writes row `row`'s values into the shape's linked fields.
bool writeRow(const DataRecordset &r, int row, Shape &shape)
{
    const QList<DataField> before = shape.data;
    for (int c = 0; c < r.columns.size(); ++c) {
        setDataValue(shape.data, r.columns.at(c), r.rows.at(row).value(c));
    }
    return !(before == shape.data);
}

QString shapeValue(const Shape &shape, const QString &field)
{
    return field.isEmpty() ? shape.text : dataValue(shape.data, field);
}

} // namespace

DataTable dataTableFromCsv(const QString &text)
{
    const QVector<QStringList> rows = parseCsv(text);
    return tableOf(rows);
}

bool readDataTable(const QString &path, const QString &sheet, DataTable *out, QString *error)
{
    QString dummy;
    QString &message = error != nullptr ? *error : dummy;
    const QFileInfo info(path);
    if (!info.exists()) {
        message = tr("%1 is not there any more.").arg(info.fileName());
        return false;
    }
    if (info.size() > kMaxBytes) {
        message = tr("%1 is larger than 64 MB.").arg(info.fileName());
        return false;
    }
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("xlsm")) {
        return readWorkbook(path, sheet, out, &message);
    }
    if (suffix == QLatin1String("csv") || suffix == QLatin1String("txt")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            message = file.errorString();
            return false;
        }
        *out = dataTableFromCsv(QString::fromUtf8(file.readAll()));
        if (out->columns.isEmpty()) {
            message = tr("%1 holds no table.").arg(info.fileName());
            return false;
        }
        return true;
    }
    message = suffix == QLatin1String("xls")
                  ? tr("Excel 97–2003 workbooks (.xls) cannot be read; save it as .xlsx in Excel first.")
                  : tr("QindaDiagram links data from Excel workbooks (.xlsx) and CSV files.");
    return false;
}

namespace Ops {

QString addRecordset(Drawing &drawing, const DataTable &table, const QString &name, const QString &source,
                     int keyColumn, const QDateTime &when)
{
    if (table.columns.isEmpty()) {
        return {};
    }
    QString base = name.trimmed().isEmpty() ? QFileInfo(source).completeBaseName() : name.trimmed();
    if (base.isEmpty()) {
        base = tr("Data");
    }
    QString unique = base;
    for (int n = 2; std::any_of(drawing.recordsets.cbegin(), drawing.recordsets.cend(),
                                [&](const DataRecordset &r) { return r.name.compare(unique, Qt::CaseInsensitive) == 0; });
         ++n) {
        unique = QStringLiteral("%1 (%2)").arg(base).arg(n);
    }
    DataRecordset r;
    r.id = drawing.newId(QStringLiteral("r"));
    r.name = unique;
    r.source = source;
    r.sheet = table.sheet;
    r.columns = table.columns;
    r.rows = table.rows;
    r.keyColumn = keyColumn >= 0 && keyColumn < table.columns.size() ? keyColumn : -1;
    r.refreshed = when.toUTC();
    drawing.recordsets.append(r);
    return r.id;
}

bool refreshRecordset(Drawing &drawing, const QString &recordsetId, const DataTable &table, const QDateTime &when,
                      int *missing)
{
    DataRecordset *r = drawing.recordset(recordsetId);
    if (r == nullptr || table.columns.isEmpty()) {
        return false;
    }
    const QString keyName = r->keyColumn >= 0 ? r->columns.value(r->keyColumn) : QString();
    r->columns = table.columns;
    r->rows = table.rows;
    r->keyColumn = keyName.isEmpty() ? -1 : int(table.columns.indexOf(keyName));
    r->refreshed = when.toUTC();
    int gone = 0;
    for (Page &page : drawing.pages) {
        for (Shape &shape : page.shapes) {
            if (shape.dataLink.recordset != recordsetId) {
                continue;
            }
            const int row = r->rowOf(shape.dataLink.row);
            if (row < 0) {
                ++gone;
            } else {
                writeRow(*r, row, shape);
            }
        }
    }
    if (missing != nullptr) {
        *missing = gone;
    }
    return true;
}

bool removeRecordset(Drawing &drawing, const QString &recordsetId)
{
    const qsizetype before = drawing.recordsets.size();
    drawing.recordsets.removeIf([&](const DataRecordset &r) { return r.id == recordsetId; });
    if (drawing.recordsets.size() == before) {
        return false;
    }
    for (Page &page : drawing.pages) {
        for (Shape &shape : page.shapes) {
            if (shape.dataLink.recordset == recordsetId) {
                shape.dataLink = {};
            }
        }
    }
    return true;
}

bool linkShapes(const Drawing &drawing, Page &page, const QStringList &shapeIds, const QString &recordsetId, int row)
{
    const DataRecordset *r = drawing.recordset(recordsetId);
    if (r == nullptr || row < 0 || row >= r->rows.size()) {
        return false;
    }
    const DataLink link{recordsetId, r->rowKey(row)};
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (!shapeIds.contains(shape.id) || shape.locked) {
            continue;
        }
        changed = writeRow(*r, row, shape) || changed;
        if (shape.dataLink != link) {
            shape.dataLink = link;
            changed = true;
        }
    }
    return changed;
}

int autoLink(const Drawing &drawing, Page &page, const QString &recordsetId, int column, const QString &shapeField,
             const QStringList &ids)
{
    const DataRecordset *r = drawing.recordset(recordsetId);
    if (r == nullptr || column < 0 || column >= r->columns.size()) {
        return 0;
    }
    // Matches first, links after: linking writes into the page's shapes.
    QList<QPair<QString, int>> matches;
    for (const Shape &shape : std::as_const(page.shapes)) {
        if ((!ids.isEmpty() && !ids.contains(shape.id)) || shape.locked || isContainer(shape)) {
            continue;
        }
        const QString value = shapeValue(shape, shapeField).trimmed();
        if (value.isEmpty()) {
            continue;
        }
        for (int row = 0; row < r->rows.size(); ++row) {
            if (r->rows.at(row).value(column).trimmed().compare(value, Qt::CaseInsensitive) == 0) {
                matches.append({shape.id, row});
                break;
            }
        }
    }
    int linked = 0;
    for (const auto &[shapeId, row] : std::as_const(matches)) {
        linked += linkShapes(drawing, page, {shapeId}, recordsetId, row) ? 1 : 0;
    }
    return linked;
}

bool unlinkShapes(Page &page, const QStringList &ids)
{
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id) && shape.dataLink.linked()) {
            shape.dataLink = {};
            changed = true;
        }
    }
    return changed;
}

QStringList shapesFromRows(Drawing &drawing, Page &page, const QString &recordsetId, const QList<int> &rows,
                           const QString &stencilId, QPointF origin, int textColumn)
{
    const DataRecordset *r = drawing.recordset(recordsetId);
    const Stencil *stencil = Diagram::stencil(stencilId);
    if (r == nullptr || stencil == nullptr) {
        return {};
    }
    // Five to a row, a stencil's size apart plus a gap.
    const double stepX = stencil->size.width() + 36;
    const double stepY = stencil->size.height() + 36;
    QStringList out;
    for (const int row : rows) {
        if (row < 0 || row >= r->rows.size()) {
            continue;
        }
        const int k = int(out.size());
        const QPointF centre = origin + QPointF((k % 5) * stepX, (k / 5) * stepY);
        const QString text = textColumn >= 0 ? r->rows.at(row).value(textColumn) : QString();
        const QString id = addShape(drawing, page, stencilId, centre, text);
        if (!id.isEmpty()) {
            linkShapes(drawing, page, {id}, recordsetId, row);
            out.append(id);
        }
    }
    return out;
}

} // namespace Ops

} // namespace QindaOffice::Diagram
