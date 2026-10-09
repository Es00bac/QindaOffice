// SPDX-License-Identifier: GPL-3.0-or-later
// DatasheetModel: loading tables and query results into the grid, sort,
// filters, totals and labels. Edits are in datasheet_edit.cpp.
#include "datasheet_model.h"

#include "base_document.h"
#include "base_macro.h"
#include "datasheet_source.h"
#include "pivot_source.h"

#include <qindatk/editable_grid_model.h>

#include <cmath>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QString valueKey(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

qreal columnWidth(Engine::FieldType type, bool lookup)
{
    // Logical grid units (the grid's default is Theme size.tableColumn).
    if (lookup) {
        return 170;
    }
    switch (type) {
    case Engine::FieldType::AutoNumber: return 64;
    case Engine::FieldType::YesNo: return 100;
    case Engine::FieldType::Integer: return 96;
    case Engine::FieldType::Number:
    case Engine::FieldType::Currency:
    case Engine::FieldType::Date: return 110;
    case Engine::FieldType::DateTime: return 150;
    case Engine::FieldType::LongText: return 240;
    default: return 170;
    }
}

} // namespace

DatasheetModel::DatasheetModel(QObject *parent)
    : QObject(parent)
    , m_grid(new QindaTK::EditableGridModel(this))
    , m_source(std::make_unique<DatasheetSource>(this))
    , m_pivotGrid(new QindaTK::EditableGridModel(this))
    , m_pivotSource(std::make_unique<PivotSource>())
{
    m_grid->setSource(m_source.get());
    m_grid->setSourceCommits(true);
    m_pivotGrid->setSource(m_pivotSource.get());
}

DatasheetModel::~DatasheetModel()
{
    // AGENT-GUARD: the grids must stop reading their sources before they die.
    m_grid->setSource(nullptr);
    m_pivotGrid->setSource(nullptr);
}

void DatasheetModel::setDocument(BaseDocument *document)
{
    m_document = document;
    const auto rebind = [this] {
        Engine::BaseFile *f = file();
        if (f == nullptr) {
            return;
        }
        connect(f, &Engine::BaseFile::dataChanged, this, [this](const QStringList &tables) {
            if (m_selfEdit || m_kind.isEmpty()) {
                return;
            }
            if (m_kind == QLatin1String("query") || tables.isEmpty() || tables.contains(m_name, Qt::CaseInsensitive)) {
                reload(true);
            }
        });
        connect(f, &Engine::BaseFile::schemaChanged, this, [this] {
            if (m_kind == QLatin1String("table") && file() != nullptr && !file()->tableExists(m_name)) {
                close();
            } else if (!m_kind.isEmpty()) {
                reload(true);
            }
        });
    };
    connect(document, &BaseDocument::fileReplaced, this, [this, rebind] {
        close();
        rebind();
    });
    rebind();
}

Engine::BaseFile *DatasheetModel::file() const
{
    return m_document != nullptr ? m_document->file() : nullptr;
}

QVariantList DatasheetModel::columns() const
{
    QVariantList out;
    for (int i = 0; i < m_columns.size(); ++i) {
        QVariantMap m = m_columns.at(i).toMap();
        m.insert(QStringLiteral("lookup"), !m_lookups.value(i).isEmpty());
        // Tk.FilterBar's field shape (dates filter as text).
        const Engine::FieldType t = m_columns.at(i).type;
        const bool lookup = !m_lookups.value(i).isEmpty();
        m.insert(QStringLiteral("id"), m_columns.at(i).name);
        m.insert(QStringLiteral("filterType"), lookup                                    ? QStringLiteral("integer")
                                               : t == Engine::FieldType::YesNo           ? QStringLiteral("boolean")
                                               : t == Engine::FieldType::Integer || t == Engine::FieldType::AutoNumber
                                                   ? QStringLiteral("integer")
                                               : Engine::isNumeric(t) ? QStringLiteral("number")
                                                                      : QStringLiteral("text"));
        out.append(m);
    }
    return out;
}

QVariantList DatasheetModel::filters() const
{
    QVariantList out;
    for (const Engine::FilterClause &f : m_filters) {
        out.append(QVariantMap{{QStringLiteral("field"), f.field}, {QStringLiteral("op"), f.op},
                               {QStringLiteral("value"), f.value}});
    }
    if (!m_condition.isEmpty()) {
        out.append(QVariantMap{{QStringLiteral("field"), QString()}, {QStringLiteral("op"), QStringLiteral("where")},
                               {QStringLiteral("value"), m_conditionText}});
    }
    return out;
}

QVariantMap DatasheetModel::applyWhere(const QString &condition)
{
    if (file() == nullptr || m_kind != QLatin1String("table")) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Open a table to filter its records.")}};
    }
    const Engine::CompiledWhere where = Engine::compileWhere(*file(), m_name, condition);
    if (!where.ok()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), where.error}};
    }
    m_condition = condition.trimmed().isEmpty() ? QString() : where.sql;
    m_conditionText = condition.trimmed();
    reload(true);
    emit queryChanged();
    return {{QStringLiteral("ok"), m_error.isEmpty()}, {QStringLiteral("error"), m_error}};
}

QVariantList DatasheetModel::sort() const
{
    QVariantList out;
    for (const Engine::SortKey &k : m_sort) {
        out.append(QVariantMap{{QStringLiteral("field"), k.field}, {QStringLiteral("descending"), k.descending}});
    }
    return out;
}

bool DatasheetModel::openTable(const QString &name)
{
    if (file() == nullptr || !file()->tableExists(name)) {
        setError(tr("There is no table named %1.").arg(name));
        return false;
    }
    m_kind = QStringLiteral("table");
    m_name = name;
    m_linkField.clear();
    m_linkValue = QVariant();
    m_condition.clear();
    m_conditionText.clear();
    m_sort.clear();
    m_filters.clear();
    m_matchAny = false;
    m_totalFunctions.clear();
    m_pending.clear();
    setPending(QString());
    reload(false);
    emit queryChanged();
    return m_error.isEmpty();
}

bool DatasheetModel::openLinked(const QString &table, const QString &linkField, const QVariant &linkValue)
{
    if (!openTable(table)) {
        return false;
    }
    m_linkField = linkField;
    m_linkValue = linkValue.isNull() ? QVariant() : linkValue;
    reload(false);
    return m_error.isEmpty();
}

bool DatasheetModel::showQuery(const QString &name, const QString &sql, const QList<Engine::QueryParameter> &declared,
                               const QVariantMap &parameters, const QList<Engine::OutputColumn> &hints)
{
    m_kind = QStringLiteral("query");
    m_name = name;
    m_run = {sql, declared, parameters, hints};
    m_sort.clear();
    m_filters.clear();
    m_matchAny = false;
    m_totalFunctions.clear();
    m_pending.clear();
    setPending(QString());
    reload(false);
    emit queryChanged();
    return m_error.isEmpty();
}

void DatasheetModel::close()
{
    m_kind.clear();
    m_name.clear();
    m_linkField.clear();
    m_linkValue = QVariant();
    m_columns.clear();
    m_lookups.clear();
    m_rowids.clear();
    m_rowIndex.clear();
    m_values.clear();
    m_pending.clear();
    m_totals.clear();
    m_totalRows = 0;
    m_editable = false;
    rebuildGrid();
    emit sourceChanged();
    emit rowsChanged();
    emit totalsChanged();
}

void DatasheetModel::refresh()
{
    if (!m_kind.isEmpty()) {
        reload(true);
    }
}

void DatasheetModel::reload(bool keepPosition)
{
    Q_UNUSED(keepPosition); // the grid keeps its active cell by id
    Engine::BaseFile *f = file();
    if (f == nullptr || m_kind.isEmpty()) {
        return;
    }
    const QStringList oldColumns = [this] {
        QStringList names;
        for (const Engine::ColumnInfo &c : m_columns) {
            names.append(c.name);
        }
        return names;
    }();
    setError(QString());
    if (m_kind == QLatin1String("table")) {
        Engine::RowQuery query;
        query.sort = m_sort;
        query.filters = m_filters;
        query.matchAny = m_matchAny;
        query.condition = m_condition;
        if (!m_linkField.isEmpty()) {
            // A subform: its parent's records only; other filters narrow them.
            query.matchAny = false;
            query.filters.append({m_linkField, m_linkValue.isValid() ? QStringLiteral("eq") : QStringLiteral("isNull"),
                                  m_linkValue.toString()});
        }
        Engine::RowSet set = Engine::Records::read(*f, m_name, query);
        if (!m_linkField.isEmpty() && !m_linkValue.isValid()) {
            set.rowids.clear(); // the parent is a new record: nothing to show or add yet
            set.values.clear();
            set.totalRows = 0;
            set.editable = false;
        }
        if (!set.error.isEmpty()) {
            setError(set.error);
            if (set.columns.isEmpty()) {
                return;
            }
        }
        m_columns = set.columns;
        m_rowids = set.rowids;
        m_values = set.values;
        m_totalRows = set.totalRows;
        m_truncated = set.truncated;
        m_editable = set.editable;
    } else {
        const Engine::QueryResult result = Engine::runSelect(*f, m_run.sql, m_run.declared, m_run.parameters, 100000,
                                                             m_run.hints);
        if (!result.ok) {
            setError(result.error);
            return;
        }
        m_columns.clear();
        for (const Engine::OutputColumn &o : result.columns) {
            Engine::ColumnInfo c;
            c.name = o.name;
            c.type = o.type;
            c.readOnly = true;
            if (!o.sourceTable.isEmpty()) {
                for (const Engine::ColumnInfo &src : Engine::Records::columns(*f, o.sourceTable)) {
                    if (src.name == o.sourceField) {
                        c.lookup = src.lookup;
                    }
                }
            }
            m_columns.append(c);
        }
        m_values = result.rows;
        m_rowids.clear();
        for (int i = 0; i < m_values.size(); ++i) {
            m_rowids.append(i + 1);
        }
        // Client-side sort for results (the query's own ORDER BY comes first).
        if (!m_sort.isEmpty()) {
            const int column = columnIndex(m_sort.first().field);
            const bool desc = m_sort.first().descending;
            QList<int> order(m_values.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const QVariant &x = m_values.at(a).at(column);
                const QVariant &y = m_values.at(b).at(column);
                const int cmp = QVariant::compare(x, y) == QPartialOrdering::Less ? -1
                                : QVariant::compare(x, y) == QPartialOrdering::Greater ? 1
                                                                                       : 0;
                return desc ? cmp > 0 : cmp < 0;
            });
            QList<QVariantList> sorted;
            for (const int i : order) {
                sorted.append(m_values.at(i));
            }
            m_values = sorted;
        }
        m_totalRows = m_values.size();
        m_truncated = result.truncated;
        m_editable = false;
    }
    m_rowIndex.clear();
    for (int i = 0; i < m_rowids.size(); ++i) {
        m_rowIndex.insert(m_rowids.at(i), i);
    }
    m_lookups.clear();
    for (const Engine::ColumnInfo &c : std::as_const(m_columns)) {
        QHash<QString, QString> map;
        if (c.lookup.isSet() && !c.lookup.displayField.isEmpty()) {
            for (const auto &[value, text] : Engine::Records::lookupChoices(*f, c.lookup)) {
                map.insert(valueKey(value), text);
            }
        }
        m_lookups.append(map);
    }
    QStringList newColumns;
    for (const Engine::ColumnInfo &c : std::as_const(m_columns)) {
        newColumns.append(c.name);
    }
    rebuildGrid();
    recomputeTotals();
    if (newColumns != oldColumns) {
        emit sourceChanged();
    } else {
        emit sourceChanged(); // editable/readOnly may differ between sources with equal columns
    }
    emit rowsChanged();
}

void DatasheetModel::rebuildGrid()
{
    m_grid->setReadOnly(false); // identity changes are refused while read-only
    QStringList columnIds;
    for (const Engine::ColumnInfo &c : std::as_const(m_columns)) {
        if (m_linkField.isEmpty() || c.name.compare(m_linkField, Qt::CaseInsensitive) != 0) {
            columnIds.append(c.name); // a subform hides its link
        }
    }
    QStringList rowIds;
    rowIds.reserve(m_rowids.size() + 1);
    for (const qint64 id : std::as_const(m_rowids)) {
        rowIds.append(QStringLiteral("r%1").arg(id));
    }
    if (m_editable) {
        rowIds.append(QStringLiteral("new"));
    }
    if (m_grid->columns() > 0 && columnIds.isEmpty()) {
        m_grid->setDimensions(0, 0);
    }
    m_grid->setColumnIds(columnIds);
    m_grid->setRowIds(rowIds);
    for (int i = 0; i < m_columns.size(); ++i) {
        if (columnIds.contains(m_columns.at(i).name)) {
            m_grid->requestColumnResize(m_columns.at(i).name,
                                        columnWidth(m_columns.at(i).type, !m_lookups.value(i).isEmpty()));
        }
    }
    m_grid->notifySourceChanged(0, int(rowIds.size()), 0, int(columnIds.size()));
    m_grid->setReadOnly(!m_editable);
}

int DatasheetModel::columnIndex(const QString &id) const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns.at(i).name == id) {
            return i;
        }
    }
    return -1;
}

qint64 DatasheetModel::rowidOf(const QString &rowId) const
{
    return rowId.startsWith(QLatin1Char('r')) ? rowId.mid(1).toLongLong() : 0;
}

int DatasheetModel::rowOf(const QString &rowId) const
{
    return m_rowIndex.value(rowidOf(rowId), -1);
}

QString DatasheetModel::display(int row, int column) const
{
    const QVariant v = m_values.at(row).value(column);
    const QHash<QString, QString> &lookup = m_lookups.at(column);
    if (!lookup.isEmpty() && v.isValid()) {
        return lookup.value(valueKey(v), v.toString());
    }
    const Engine::CurrencyFormat currency = file() != nullptr ? file()->currency() : Engine::CurrencyFormat{};
    return m_columns.at(column).display(v, currency); // M80: text through its input mask
}

QString DatasheetModel::edit(int row, int column) const
{
    const QVariant v = m_values.at(row).value(column);
    if (!m_lookups.at(column).isEmpty()) {
        return display(row, column);
    }
    const Engine::CurrencyFormat currency = file() != nullptr ? file()->currency() : Engine::CurrencyFormat{};
    return Engine::editText(m_columns.at(column).type, v, currency);
}

QString DatasheetModel::columnLabel(const QString &columnId) const
{
    const int i = columnIndex(columnId);
    return i < 0 ? columnId : m_columns.at(i).label();
}

QString DatasheetModel::cellText(const QString &rowId, const QString &columnId) const
{
    const int column = columnIndex(columnId);
    const int row = rowOf(rowId);
    return column < 0 || row < 0 ? QString() : display(row, column);
}

void DatasheetModel::sortBy(const QString &field, bool descending)
{
    if (columnIndex(field) < 0) {
        return;
    }
    m_sort = {{field, descending}};
    reload(true);
    emit queryChanged();
}

bool DatasheetModel::filterBySelection(const QString &rowId, const QString &columnId, bool exclude)
{
    const int column = columnIndex(columnId);
    const int row = rowOf(rowId);
    if (column < 0 || row < 0 || m_kind != QLatin1String("table")) {
        return false;
    }
    const QVariant v = m_values.at(row).at(column);
    Engine::FilterClause clause;
    clause.field = m_columns.at(column).name;
    if (!v.isValid() || m_columns.at(column).type == Engine::FieldType::File) {
        // A file filters by having one or not (its bytes are no filter value).
        const bool none = !v.isValid();
        clause.op = exclude == none ? QStringLiteral("isNotNull") : QStringLiteral("isNull");
    } else {
        clause.op = exclude ? QStringLiteral("ne") : QStringLiteral("eq");
        // The stored value's own text (a lookup filters by its key).
        clause.value = m_lookups.at(column).isEmpty()
                           ? Engine::editText(m_columns.at(column).type, v, file()->currency())
                           : v.toString();
    }
    if (m_matchAny && !m_filters.isEmpty()) {
        m_filters.clear(); // a selection filter narrows; it does not widen an OR filter
    }
    m_matchAny = false;
    m_filters.append(clause);
    reload(true);
    emit queryChanged();
    return m_error.isEmpty();
}

bool DatasheetModel::setFilters(const QVariantList &clauses, const QString &conjunction)
{
    m_matchAny = conjunction == QLatin1String("or");
    QList<Engine::FilterClause> next;
    for (const QVariant &v : clauses) {
        const QVariantMap m = v.toMap();
        next.append({m.value(QStringLiteral("field")).toString(), m.value(QStringLiteral("op")).toString(),
                     m.value(QStringLiteral("value")).toString()});
    }
    m_filters = next;
    reload(true);
    emit queryChanged();
    return m_error.isEmpty();
}

void DatasheetModel::clearFilters()
{
    m_filters.clear();
    m_condition.clear();
    m_conditionText.clear();
    m_matchAny = false;
    m_sort.clear();
    reload(true);
    emit queryChanged();
}

void DatasheetModel::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

void DatasheetModel::setPending(const QString &message)
{
    if (m_pendingMessage != message) {
        m_pendingMessage = message;
        emit pendingChanged();
    }
}

} // namespace QindaOffice::Apps::Base
