// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Importing an Access database (M49, completed in M65 — D-049 §7, D-074):
// the chosen tables with their records, then the relationships between
// them, then the saved select queries — over an AccessReader (mdb-tools,
// or captured output in tests; base_access.h).
//
// AGENT-CONTRACT (with BaseService::importAccess and AccessImportDialog):
//  * With mdb-schema's DDL a table keeps its Access design: types
//    (AutoNumber, Currency, Date vs Date/Time, Memo, OLE/binary → a file
//    field), required fields, descriptions, the primary key with its values
//    (relationships depend on them), unique and plain indexes. Without it,
//    types are detected from the text as for CSV.
//  * Every change is returned in `changes`, in order, so the caller pushes
//    the whole import as ONE undo step. A table whose records cannot be
//    imported is removed again; nothing is half-imported.
//  * What does not come across is never dropped silently: the report's
//    `skipped` names each table, field, relationship and query left out and
//    why; `notes` names each change of meaning (a Currency field with
//    fractions of a cent becoming a decimal number, rows that did not fit).

#include "base_access.h"
#include "base_file.h"

#include <QVariantMap>

namespace QindaOffice::Base {

struct AccessImportOptions {
    QStringList tables; // Access names, as AccessReader::tables() lists them
    bool queries = true;
};

struct AccessImportReport {
    bool ok = false;     // at least one table was imported
    QString error;       // nothing could be read
    struct Table {
        QString source;
        QString table;   // the name it was imported as
        int rows = 0;
    };
    QList<Table> tables;
    QStringList relationships; // "Dues.MemberID → Members.MemberID"
    QStringList queries;       // imported query names
    QStringList skipped;       // "Query Overdue: it joins 2 tables, …"
    QStringList notes;
    [[nodiscard]] QVariantMap toMap() const;
};

AccessImportReport importAccessDatabase(BaseFile &file, AccessReader &reader, const AccessImportOptions &options,
                                        QList<Change> *changes);

} // namespace QindaOffice::Base
