// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Datasheet records (M49, D-049 §5): reading a table's rows with sort and
// filter, and every edit a datasheet or a form makes — cell updates, new
// records, deletes, tab-separated paste — each as ONE captured Change (one
// undo step), validated per field type with messages people understand.
//
// AGENT-CONTRACT (row identity): a record is addressed by its SQLite rowid
// (for INTEGER PRIMARY KEY tables that IS the key). WITHOUT ROWID tables
// read fine but are not editable here. Values cross as storage values
// (base_types.h); inputs may be text or typed QVariants.

#include "base_file.h"
#include "base_schema.h"

#include <QHash>
#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

struct SortKey {
    QString field;
    bool descending = false;
};

// op: eq | ne | lt | le | gt | ge | contains | notContains | isNull | isNotNull
struct FilterClause {
    QString field;
    QString op = QStringLiteral("eq");
    QString value; // edit text, parsed with the field's type
};

struct RowQuery {
    QList<SortKey> sort;
    QList<FilterClause> filters;
    bool matchAny = false; // filters joined with OR instead of AND
    int limit = 100000;
    // M80: an ApplyFilter condition, ANDed with the filters. SQL from
    // compileWhere (base_macro.h) only — never text a user typed.
    QString condition;
};

struct ColumnInfo {
    QString name;
    QString caption;
    FieldType type = FieldType::Text;
    bool required = false;
    bool primaryKey = false;
    bool readOnly = false; // AutoNumber, Other
    QString defaultValue;
    LookupDef lookup;
    // M80 (D-089): typed text goes through the mask (resolveInput) before it
    // is parsed; the rule is enforced by the database (base_rules.h) and is
    // here only so editors can say what it is.
    QString inputMask;
    QString validationRule;
    QString validationText;
    [[nodiscard]] QString label() const { return caption.isEmpty() ? name : caption; }
    [[nodiscard]] QVariantMap toMap() const;
    // A stored value as the datasheet shows it: displayText, through the
    // input mask for text ("3035551234" → "(303) 555-1234").
    [[nodiscard]] QString display(const QVariant &stored, const CurrencyFormat &currency) const;
};

struct RowSet {
    QString table;
    QList<ColumnInfo> columns;
    QList<qint64> rowids;
    QList<QVariantList> values; // storage values, one list per row
    qint64 totalRows = 0;       // rows matching the filter (may exceed the limit)
    bool truncated = false;
    bool editable = true;
    QString error;
    [[nodiscard]] int columnIndex(const QString &name) const;
};

struct CellEdit {
    qint64 rowid = 0;
    QString field;
    QVariant input; // text or a typed value; invalid = empty
};

class Records {
public:
    [[nodiscard]] static QList<ColumnInfo> columns(const BaseFile &file, const QString &table);
    [[nodiscard]] static RowSet read(const BaseFile &file, const QString &table, const RowQuery &query = {});
    [[nodiscard]] static std::optional<QVariantList> readRow(const BaseFile &file, const QString &table, qint64 rowid,
                                                             const QList<ColumnInfo> &columns);
    // SQL (" WHERE …" or "") and bindings for the filters; `error` on a
    // value that does not fit its field.
    [[nodiscard]] static QString whereClause(const QList<ColumnInfo> &columns, const QList<FilterClause> &filters,
                                             const CurrencyFormat &currency, QVariantList *bindings, QString *error,
                                             bool matchAny = false);

    // Aggregates over the filtered rows: sum | average | count | min | max.
    // Returns the storage value (currency sums stay integer minor units;
    // averages are doubles) and `display` text.
    // `condition`: RowQuery::condition (M80), ANDed with the filters.
    [[nodiscard]] static QVariant total(const BaseFile &file, const QString &table, const QString &field,
                                        const QString &function, const QList<FilterClause> &filters,
                                        QString *display, QString *error = nullptr, bool matchAny = false,
                                        const QString &condition = {});

    // Lookup choices: value → display text (at most `limit`, sorted by display).
    [[nodiscard]] static QList<QPair<QVariant, QString>> lookupChoices(const BaseFile &file, const LookupDef &lookup,
                                                                       int limit = 20000);
    // A typed value, or a display text resolved through the lookup.
    [[nodiscard]] static Parsed resolveInput(const BaseFile &file, const ColumnInfo &column, const QVariant &input);

    // ---- edits: each is one captured Change ----
    static Result updateCells(BaseFile &file, const QString &table, const QList<CellEdit> &edits, Change *change);
    static Result insertRecord(BaseFile &file, const QString &table, const QVariantMap &values, qint64 *rowid,
                               Change *change);
    static Result deleteRecords(BaseFile &file, const QString &table, const QList<qint64> &rowids, Change *change);
    // What deleting these records does to related tables ("Also deletes
    // 3 donations", "2 projects will lose their client"); a blocking
    // relationship makes `blocked` true.
    [[nodiscard]] static QStringList deleteImpact(const BaseFile &file, const QString &table,
                                                  const QList<qint64> &rowids, bool *blocked);
    // Tab-separated text (Excel/Calc clipboard) pasted from the cell at
    // (`startRow` in `rowOrder`, editable column `startColumn`): existing
    // rows are updated in view order, rows past the end are appended.
    static Result pasteText(BaseFile &file, const QString &table, const QList<qint64> &rowOrder, int startRow,
                            const QString &startColumn, const QString &text, Change *change, int *updated,
                            int *inserted);
    [[nodiscard]] static QList<QStringList> parseTabular(const QString &text);

    // ---- file fields (base_file_field.h; base_records_file.cpp) ----
    // The bytes in one File cell: empty for no file; nullopt with `error`
    // when the record or the field is missing.
    [[nodiscard]] static std::optional<QByteArray> readFile(const BaseFile &file, const QString &table, qint64 rowid,
                                                            const QString &field, QString *error);
    // Puts `bytes` in a File cell (a null QByteArray clears it) as one
    // captured Change. Refuses a field that is not a File field and files
    // over kFileFieldLimit.
    static Result setFile(BaseFile &file, const QString &table, qint64 rowid, const QString &field,
                          const QByteArray &bytes, Change *change);

    // SQLite's constraint messages in the user's words.
    [[nodiscard]] static QString explain(const QString &sqliteError, const QList<ColumnInfo> &columns,
                                         const QString &table);
};

} // namespace QindaOffice::Base
