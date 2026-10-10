// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DatasheetModel: the rows of one table or one query result, shown by a
// Tk.EditableGrid (M49, D-049 §5). The grid's EditableGridModel runs with
// sourceCommits: every committed cell goes through base_engine (typed,
// validated, captured) and lands on the document's undo stack; nothing is
// kept in the grid's override layer.
//
// AGENT-CONTRACT (with qml/DatasheetView.qml and the host):
//  * Row ids are "r<rowid>"; a table datasheet ends with the new-record row
//    "new". Typing there collects values until the record's required fields
//    are filled, then inserts it (pendingMessage says what is missing).
//  * Column ids are the field names (query results: the output names).
//  * Cells show display text (money with its symbol, lookups as the related
//    record's name) and edit from edit text (money without grouping).
//  * Query results are read-only (readOnly on the grid model).
//  * The model reloads itself when the file reports a data or schema
//    change touching its source, keeping sort, filters and totals.

#include "base_query.h"
#include "base_records.h"

#include <qindatk/editable_grid_model.h>

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Apps::Base {

class BaseDocument;
class DatasheetSource;
class PivotSource;

class DatasheetModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by BaseService.")
    Q_PROPERTY(QindaTK::EditableGridModel *grid READ grid CONSTANT)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY sourceChanged)
    Q_PROPERTY(bool editable READ editable NOTIFY sourceChanged)
    Q_PROPERTY(int recordCount READ recordCount NOTIFY rowsChanged)
    Q_PROPERTY(qint64 totalRows READ totalRows NOTIFY rowsChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList filters READ filters NOTIFY queryChanged)
    Q_PROPERTY(QVariantList sort READ sort NOTIFY queryChanged)
    Q_PROPERTY(bool totalsVisible READ totalsVisible WRITE setTotalsVisible NOTIFY totalsChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY totalsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString pendingMessage READ pendingMessage NOTIFY pendingChanged)
    // The rows as typed values for Tk.QueryController (pivot_source.h).
    Q_PROPERTY(QindaTK::EditableGridModel *pivotGrid READ pivotGrid CONSTANT)
    Q_PROPERTY(QVariantMap pivotSchema READ pivotSchema NOTIFY pivotChanged)
public:
    explicit DatasheetModel(QObject *parent = nullptr);
    ~DatasheetModel() override;

    void setDocument(BaseDocument *document);
    [[nodiscard]] QindaTK::EditableGridModel *grid() const { return m_grid; }
    [[nodiscard]] QString sourceKind() const { return m_kind; }
    [[nodiscard]] QString sourceName() const { return m_name; }
    [[nodiscard]] QVariantList columns() const;
    [[nodiscard]] bool editable() const { return m_editable; }
    [[nodiscard]] int recordCount() const { return int(m_rowids.size()); }
    [[nodiscard]] qint64 totalRows() const { return m_totalRows; }
    [[nodiscard]] bool truncated() const { return m_truncated; }
    [[nodiscard]] QVariantList filters() const;
    [[nodiscard]] QVariantList sort() const;
    [[nodiscard]] bool totalsVisible() const { return m_totalsVisible; }
    void setTotalsVisible(bool visible);
    [[nodiscard]] QVariantMap totals() const { return m_totals; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString pendingMessage() const { return m_pendingMessage; }

    Q_INVOKABLE bool openTable(const QString &name);
    // M80 (a form's subform, base_form_controls.h): the records of `table`
    // whose `linkField` equals `linkValue`, the link column hidden and set on
    // every new record. An invalid `linkValue` (the form's new record) shows
    // no records and takes none.
    Q_INVOKABLE bool openLinked(const QString &table, const QString &linkField, const QVariant &linkValue);
    // A select's result (the service compiles and parameterises it).
    bool showQuery(const QString &name, const QString &sql, const QList<QindaOffice::Base::QueryParameter> &declared,
                   const QVariantMap &parameters, const QList<QindaOffice::Base::OutputColumn> &hints);
    Q_INVOKABLE void close();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void sortBy(const QString &field, bool descending);
    Q_INVOKABLE bool filterBySelection(const QString &rowId, const QString &columnId, bool exclude);
    // Tk.FilterBar clauses: [{field, type, op, value}], joined by
    // conjunction "and" | "or".
    Q_INVOKABLE bool setFilters(const QVariantList &clauses, const QString &conjunction = QStringLiteral("and"));
    Q_INVOKABLE void clearFilters();
    // M80 (a macro's ApplyFilter): only the records meeting an Access where
    // condition over the table ("[city] = \"Portland\""); {ok, error}.
    Q_INVOKABLE QVariantMap applyWhere(const QString &condition);
    // function: sum | average | count | min | max | "" (none)
    Q_INVOKABLE void setTotal(const QString &field, const QString &function);
    Q_INVOKABLE QString columnLabel(const QString &columnId) const;
    Q_INVOKABLE QString cellText(const QString &rowId, const QString &columnId) const;
    // M86: the text a cell's editor starts from (a hyperlink's whole stored
    // text, money without its symbol) — datasheet_edit.cpp.
    Q_INVOKABLE QString cellEditText(const QString &rowId, const QString &columnId) const;
    Q_INVOKABLE QVariantList lookupChoices(const QString &columnId) const;
    Q_INVOKABLE QVariantMap setCell(const QString &rowId, const QString &columnId, const QVariant &value);
    Q_INVOKABLE QVariantMap deleteImpact(const QStringList &rowIds) const;
    Q_INVOKABLE QVariantMap deleteRows(const QStringList &rowIds);
    Q_INVOKABLE QVariantMap pasteText(const QString &rowId, const QString &columnId, const QString &text);
    // Tab-separated text of an index rectangle (display text, like Access).
    Q_INVOKABLE QString copyText(int firstRow, int firstColumn, int lastRow, int lastColumn) const;
    // Empties the editable cells of an index rectangle as one step (Cut).
    Q_INVOKABLE QVariantMap clearCells(int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE QVariantMap find(const QString &text, const QString &fromRowId, const QString &fromColumnId,
                                 bool forward) const;
    Q_INVOKABLE QVariantMap savePendingRecord();
    Q_INVOKABLE void cancelPendingRecord();
    // Refreshes pivotGrid/pivotSchema from the rows shown now.
    Q_INVOKABLE void preparePivot();
    [[nodiscard]] QindaTK::EditableGridModel *pivotGrid() const { return m_pivotGrid; }
    [[nodiscard]] QVariantMap pivotSchema() const { return m_pivotSchema; }

    // Export: the header and rows as shown (edit text, so numbers stay
    // numbers), and the raw storage for typed exports.
    [[nodiscard]] QStringList exportHeader() const;
    [[nodiscard]] QList<QStringList> exportRows() const;
    [[nodiscard]] QList<QindaOffice::Base::ColumnInfo> columnInfo() const { return m_columns; }
    [[nodiscard]] QList<QVariantList> storageRows() const { return m_values; }

signals:
    void sourceChanged();
    void rowsChanged();
    void queryChanged();
    void totalsChanged();
    void errorChanged();
    void pendingChanged();
    void pivotChanged();
    void messageRequested(const QString &title, const QString &text, const QString &variant);

private:
    friend class DatasheetSource;
    QindaOffice::Base::BaseFile *file() const;
    void reload(bool keepPosition);
    void rebuildGrid();
    void recomputeTotals();
    void setError(const QString &error);
    void setPending(const QString &message);
    bool commit(const QVariantList &changes, QString *error);
    [[nodiscard]] int columnIndex(const QString &id) const;
    [[nodiscard]] qint64 rowidOf(const QString &rowId) const;
    [[nodiscard]] int rowOf(const QString &rowId) const; // index into m_rowids, -1
    [[nodiscard]] QString display(int row, int column) const;
    [[nodiscard]] QString edit(int row, int column) const;

    QPointer<BaseDocument> m_document;
    QindaTK::EditableGridModel *m_grid = nullptr;
    std::unique_ptr<DatasheetSource> m_source;
    QindaTK::EditableGridModel *m_pivotGrid = nullptr;
    std::unique_ptr<PivotSource> m_pivotSource;
    QVariantMap m_pivotSchema;
    int m_pivotRevision = 0;
    QString m_kind;
    QString m_name;
    QList<QindaOffice::Base::ColumnInfo> m_columns;
    QList<QHash<QString, QString>> m_lookups; // per column: stored value → display text
    QList<qint64> m_rowids;
    QHash<qint64, int> m_rowIndex;
    QList<QVariantList> m_values;
    QList<QindaOffice::Base::SortKey> m_sort;
    QList<QindaOffice::Base::FilterClause> m_filters;
    bool m_matchAny = false;
    QVariantMap m_totalFunctions; // field → function
    QVariantMap m_totals;
    QVariantMap m_pending;        // field → value typed into the new-record row
    QString m_condition;          // M80: applyWhere's compiled SQL; its text is m_conditionText
    QString m_conditionText;
    QString m_linkField;          // M80: a subform's link (openLinked); "" otherwise
    QVariant m_linkValue;
    QString m_error;
    QString m_pendingMessage;
    // The last select shown (a query result reruns it on refresh).
    struct QueryRun {
        QString sql;
        QList<QindaOffice::Base::QueryParameter> declared;
        QVariantMap parameters;
        QList<QindaOffice::Base::OutputColumn> hints;
    } m_run;
    qint64 m_totalRows = 0;
    bool m_truncated = false;
    bool m_editable = false;
    bool m_totalsVisible = false;
    bool m_selfEdit = false; // our own commit: the file's change signal needs no reload
};

} // namespace QindaOffice::Apps::Base
