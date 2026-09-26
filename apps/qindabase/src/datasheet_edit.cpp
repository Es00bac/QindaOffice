// SPDX-License-Identifier: GPL-3.0-or-later
// DatasheetModel: edits — committed cells, the new-record row, deletes,
// paste, copy and find. Each write is one Change on the document's stack.
#include "base_document.h"
#include "datasheet_model.h"
#include "pivot_source.h"

#include <qindatk/editable_grid_model.h>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap ok(const QVariantMap &extra = {})
{
    QVariantMap out = extra;
    out.insert(QStringLiteral("ok"), true);
    return out;
}

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

} // namespace

bool DatasheetModel::commit(const QVariantList &changes, QString *error)
{
    Engine::BaseFile *f = file();
    if (f == nullptr || !m_editable) {
        *error = tr("This datasheet is read-only.");
        return false;
    }
    QList<Engine::CellEdit> edits;
    bool touchedNew = false;
    for (const QVariant &v : changes) {
        const QVariantMap c = v.toMap();
        const QString rowId = c.value(QStringLiteral("rowId")).toString();
        const QString columnId = c.value(QStringLiteral("columnId")).toString();
        if (rowId == QLatin1String("new")) {
            const QString text = c.value(QStringLiteral("after")).toString();
            if (text.isEmpty()) {
                m_pending.remove(columnId);
            } else {
                m_pending.insert(columnId, text);
            }
            touchedNew = true;
            continue;
        }
        edits.append({rowidOf(rowId), columnId, c.value(QStringLiteral("after"))});
    }
    if (!edits.isEmpty()) {
        Engine::Change change;
        m_selfEdit = true;
        const Engine::Result r = Engine::Records::updateCells(*f, m_name, edits, &change);
        m_selfEdit = false;
        if (!r.ok) {
            *error = r.error;
            emit messageRequested(tr("Not saved"), r.error, QStringLiteral("warning"));
            return false;
        }
        m_document->pushChange(change);
        const QList<Engine::ColumnInfo> cols = m_columns;
        for (const Engine::CellEdit &e : std::as_const(edits)) {
            const int row = m_rowIndex.value(e.rowid, -1);
            if (row < 0) {
                continue;
            }
            if (const std::optional<QVariantList> fresh = Engine::Records::readRow(*f, m_name, e.rowid, cols)) {
                m_values[row] = *fresh;
                m_grid->notifySourceChanged(row, 1, 0, int(cols.size()));
            }
        }
        recomputeTotals();
    }
    if (touchedNew) {
        // A new record is saved as soon as its required fields are in.
        QStringList missing;
        for (const Engine::ColumnInfo &c : std::as_const(m_columns)) {
            if (c.required && !c.readOnly && c.defaultValue.isEmpty() && !m_pending.contains(c.name)) {
                missing.append(c.label());
            }
        }
        const int newRow = int(m_rowids.size());
        m_grid->notifySourceChanged(newRow, 1, 0, int(m_columns.size()));
        if (!missing.isEmpty() && !m_pending.isEmpty()) {
            setPending(tr("New record: fill in %1 to save it (Esc discards it).").arg(missing.join(QStringLiteral(", "))));
            return true;
        }
        if (!m_pending.isEmpty()) {
            const QVariantMap saved = savePendingRecord();
            if (!saved.value(QStringLiteral("ok")).toBool()) {
                *error = saved.value(QStringLiteral("error")).toString();
                return false;
            }
        }
    }
    return true;
}

QVariantMap DatasheetModel::savePendingRecord()
{
    Engine::BaseFile *f = file();
    if (f == nullptr || m_pending.isEmpty()) {
        return failed(tr("There is no new record to save."));
    }
    qint64 rowid = 0;
    Engine::Change change;
    m_selfEdit = true;
    const Engine::Result r = Engine::Records::insertRecord(*f, m_name, m_pending, &rowid, &change);
    m_selfEdit = false;
    if (!r.ok) {
        setPending(r.error);
        emit messageRequested(tr("Record not saved"), r.error, QStringLiteral("warning"));
        return failed(r.error);
    }
    m_document->pushChange(change);
    m_pending.clear();
    setPending(QString());
    const std::optional<QVariantList> fresh = Engine::Records::readRow(*f, m_name, rowid, m_columns);
    if (fresh) {
        const int index = int(m_rowids.size());
        m_rowids.append(rowid);
        m_values.append(*fresh);
        m_rowIndex.insert(rowid, index);
        ++m_totalRows;
        m_grid->insertRowsAt(index, {QStringLiteral("r%1").arg(rowid)});
        m_grid->notifySourceChanged(index, 2, 0, int(m_columns.size()));
    }
    recomputeTotals();
    emit rowsChanged();
    return ok({{QStringLiteral("rowId"), QStringLiteral("r%1").arg(rowid)}});
}

void DatasheetModel::cancelPendingRecord()
{
    if (m_pending.isEmpty()) {
        return;
    }
    m_pending.clear();
    setPending(QString());
    m_grid->notifySourceChanged(int(m_rowids.size()), 1, 0, int(m_columns.size()));
}

QVariantList DatasheetModel::lookupChoices(const QString &columnId) const
{
    QVariantList out;
    const int column = columnIndex(columnId);
    if (column < 0 || file() == nullptr || !m_columns.at(column).lookup.isSet()) {
        return out;
    }
    for (const auto &[value, text] : Engine::Records::lookupChoices(*file(), m_columns.at(column).lookup)) {
        out.append(QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}});
    }
    return out;
}

QVariantMap DatasheetModel::setCell(const QString &rowId, const QString &columnId, const QVariant &value)
{
    QString error;
    const QVariantList changes{QVariantMap{{QStringLiteral("rowId"), rowId},
                                           {QStringLiteral("columnId"), columnId},
                                           {QStringLiteral("after"), value}}};
    return commit(changes, &error) ? ok() : failed(error);
}

QVariantMap DatasheetModel::deleteImpact(const QStringList &rowIds) const
{
    QList<qint64> ids;
    for (const QString &id : rowIds) {
        if (rowOf(id) >= 0) {
            ids.append(rowidOf(id));
        }
    }
    bool blocked = false;
    const QStringList lines = file() != nullptr && m_kind == QLatin1String("table")
                                  ? Engine::Records::deleteImpact(*file(), m_name, ids, &blocked)
                                  : QStringList();
    return {{QStringLiteral("count"), ids.size()}, {QStringLiteral("lines"), lines}, {QStringLiteral("blocked"), blocked}};
}

QVariantMap DatasheetModel::deleteRows(const QStringList &rowIds)
{
    if (!m_editable || file() == nullptr) {
        return failed(tr("This datasheet is read-only."));
    }
    QList<qint64> ids;
    for (const QString &id : rowIds) {
        if (rowOf(id) >= 0) {
            ids.append(rowidOf(id));
        }
    }
    if (ids.isEmpty()) {
        return failed(tr("Select the records to delete."));
    }
    Engine::Change change;
    const Engine::Result r = Engine::Records::deleteRecords(*file(), m_name, ids, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change); // the file's change signal reloads the rows
    return ok({{QStringLiteral("count"), ids.size()}});
}

QVariantMap DatasheetModel::pasteText(const QString &rowId, const QString &columnId, const QString &text)
{
    if (!m_editable || file() == nullptr) {
        return failed(tr("This datasheet is read-only."));
    }
    const int start = rowId == QLatin1String("new") ? int(m_rowids.size()) : rowOf(rowId);
    if (start < 0) {
        return failed(tr("Select where to paste."));
    }
    int updated = 0;
    int inserted = 0;
    Engine::Change change;
    const Engine::Result r =
        Engine::Records::pasteText(*file(), m_name, m_rowids, start, columnId, text, &change, &updated, &inserted);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return ok({{QStringLiteral("updated"), updated}, {QStringLiteral("inserted"), inserted}});
}

QString DatasheetModel::copyText(int firstRow, int firstColumn, int lastRow, int lastColumn) const
{
    QStringList lines;
    for (int r = std::max(0, firstRow); r <= lastRow && r < m_values.size(); ++r) {
        QStringList cells;
        for (int c = std::max(0, firstColumn); c <= lastColumn && c < m_columns.size(); ++c) {
            QString cell = display(r, c);
            if (cell.contains(QLatin1Char('\t')) || cell.contains(QLatin1Char('\n')) || cell.contains(QLatin1Char('"'))) {
                cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                cell = QLatin1Char('"') + cell + QLatin1Char('"');
            }
            cells.append(cell);
        }
        lines.append(cells.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap DatasheetModel::clearCells(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    QVariantList changes;
    for (int r = std::max(0, firstRow); r <= lastRow && r < m_rowids.size(); ++r) {
        for (int c = std::max(0, firstColumn); c <= lastColumn && c < m_columns.size(); ++c) {
            if (!m_columns.at(c).readOnly) {
                changes.append(QVariantMap{{QStringLiteral("rowId"), QStringLiteral("r%1").arg(m_rowids.at(r))},
                                           {QStringLiteral("columnId"), m_columns.at(c).name},
                                           {QStringLiteral("after"), QString()}});
            }
        }
    }
    QString error;
    return changes.isEmpty() || commit(changes, &error) ? ok({{QStringLiteral("count"), changes.size()}}) : failed(error);
}

QVariantMap DatasheetModel::find(const QString &text, const QString &fromRowId, const QString &fromColumnId,
                                 bool forward) const
{
    const int rows = int(m_values.size());
    const int columns = int(m_columns.size());
    if (text.isEmpty() || rows == 0 || columns == 0) {
        return {{QStringLiteral("found"), false}};
    }
    const int startRow = std::max(0, rowOf(fromRowId));
    const int startColumn = std::max(0, columnIndex(fromColumnId));
    const int total = rows * columns;
    const int origin = startRow * columns + startColumn;
    for (int step = 1; step <= total; ++step) {
        const int index = ((origin + (forward ? step : -step)) % total + total) % total;
        const int r = index / columns;
        const int c = index % columns;
        if (display(r, c).contains(text, Qt::CaseInsensitive)) {
            return {{QStringLiteral("found"), true},
                    {QStringLiteral("rowId"), QStringLiteral("r%1").arg(m_rowids.at(r))},
                    {QStringLiteral("columnId"), m_columns.at(c).name},
                    {QStringLiteral("row"), r},
                    {QStringLiteral("column"), c}};
        }
    }
    return {{QStringLiteral("found"), false}};
}

void DatasheetModel::preparePivot()
{
    // Typed values: money in units, lookups as their display text, Yes/No
    // as booleans (pivot_source.h).
    const Engine::CurrencyFormat currency = file() != nullptr ? file()->currency() : Engine::CurrencyFormat{};
    double scale = 1;
    for (int i = 0; i < currency.decimals; ++i) {
        scale *= 10;
    }
    QStringList ids;
    QVariantList fields;
    for (int c = 0; c < m_columns.size(); ++c) {
        const Engine::ColumnInfo &col = m_columns.at(c);
        const bool lookup = !m_lookups.value(c).isEmpty();
        QString type = QStringLiteral("text");
        if (!lookup) {
            if (col.type == Engine::FieldType::Integer || col.type == Engine::FieldType::AutoNumber) {
                type = QStringLiteral("integer");
            } else if (col.type == Engine::FieldType::Number || col.type == Engine::FieldType::Currency) {
                type = QStringLiteral("number");
            } else if (col.type == Engine::FieldType::YesNo) {
                type = QStringLiteral("boolean");
            }
        }
        ids.append(col.name);
        fields.append(QVariantMap{{QStringLiteral("id"), col.name},
                                  {QStringLiteral("label"), col.label()},
                                  {QStringLiteral("type"), type},
                                  {QStringLiteral("nullable"), true}});
    }
    QList<QVariantList> rows;
    rows.reserve(m_values.size());
    for (int r = 0; r < m_values.size(); ++r) {
        QVariantList row;
        for (int c = 0; c < m_columns.size(); ++c) {
            const QVariant v = m_values.at(r).value(c);
            const Engine::FieldType type = m_columns.at(c).type;
            if (!v.isValid()) {
                row.append(QVariant());
            } else if (!m_lookups.value(c).isEmpty()) {
                row.append(display(r, c));
            } else if (type == Engine::FieldType::Currency) {
                row.append(v.metaType().id() == QMetaType::LongLong ? double(v.toLongLong()) / scale : v.toDouble());
            } else if (type == Engine::FieldType::Number) {
                row.append(v.toDouble());
            } else if (type == Engine::FieldType::Integer || type == Engine::FieldType::AutoNumber) {
                row.append(v.toLongLong());
            } else if (type == Engine::FieldType::YesNo) {
                row.append(v.toLongLong() != 0);
            } else {
                row.append(display(r, c));
            }
        }
        rows.append(row);
    }
    m_pivotGrid->setReadOnly(false);
    m_pivotGrid->setDimensions(0, 0);
    m_pivotSource->setRows(ids, rows);
    m_pivotGrid->setColumnIds(ids);
    m_pivotGrid->setDimensions(int(rows.size()), int(ids.size()));
    m_pivotGrid->setColumnIds(ids);
    m_pivotGrid->notifySourceChanged(0, int(rows.size()), 0, int(ids.size()));
    ++m_pivotRevision;
    m_pivotSchema = {{QStringLiteral("id"), m_name.isEmpty() ? QStringLiteral("datasheet") : m_name},
                     {QStringLiteral("revision"), QString::number(m_pivotRevision)},
                     {QStringLiteral("fields"), fields}};
    emit pivotChanged();
}

} // namespace QindaOffice::Apps::Base
