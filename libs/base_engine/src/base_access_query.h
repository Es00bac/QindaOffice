// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A saved Access query, as mdb-queries prints it, converted to a QindaBase
// query design (M65, D-074) — or the reason it cannot be.
//
// AGENT-CONTRACT (with mdb-tools' mdb-queries.c, mdbtools 1.0): the SQL is
// rebuilt from MSysQueries as
//   SELECT[ TOP n[ PERCENT]| DISTINCTROW| DISTINCT] <columns> FROM [T1],[T2]
//   [ WHERE <criteria>][ ORDER BY <first sort key>[ DESCENDING]]
// with the column expressions as Access stores them ("Members.Name",
// "[Price]*[Qty]") — but without the joins, the Group By, the Having, the
// column aliases or the second and later sort keys. So only what that text
// says completely is converted: one table, no totals, no form references
// (Forms!…), no TOP … PERCENT; anything else gets a reason the import
// reports. The design's expressions and criteria then compile through the
// ordinary query compiler (base_query.h), which is the final check — and
// turns unknown [names] into parameters, as Access does.

#include "base_query.h"

#include <QHash>

#include <optional>

namespace QindaOffice::Base {

struct AccessQueryConversion {
    std::optional<QueryDesign> design;
    QString reason; // why not, in the user's words
};

// `tables`: Access table name, lower-cased → the table it was imported as.
[[nodiscard]] AccessQueryConversion convertAccessQuery(const QString &sql, const QHash<QString, QString> &tables);
// MSysObjects.Flags → "select", "crosstab", "delete", "update", "append",
// "make-table", "data definition", "pass-through", "union".
[[nodiscard]] QString accessQueryKind(int flags);

} // namespace QindaOffice::Base
