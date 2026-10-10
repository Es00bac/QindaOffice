// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access's query wizards (M86, D-094): Simple Query, Crosstab Query, Find
// Duplicates and Find Unmatched. Each turns the wizard's answers into an
// ordinary query design — the grid the user can open, change and save —
// the way Access's wizards write theirs.
//
// AGENT-CONTRACT (with BaseService::queryWizard and qml/QueryWizardDialog.qml):
//  * Simple: the chosen fields of one table or of related tables (joined
//    by their relationships; unrelated tables are refused); Summary groups
//    by the other fields and totals the numbers (Sum Of …, Avg Of …, Min
//    Of …, Max Of …, Count Of <table>), a date grouped by day, month,
//    quarter or year through Format().
//  * Crosstab: a table's row headings (up to three), one column heading — a
//    date by year, quarter ("Qtr 1"…), month ("Jan"… as fixed headings),
//    date, or as it is — a value with Sum, Avg, Count, Min or Max, and the
//    row's own total when asked ("Total Of …").
//  * Find Duplicates: with no other field, a totals query (the repeated
//    values with NumberOfDups > 1); with other fields, every record whose
//    values repeat, through an "In (SELECT … HAVING Count(*) > 1)"
//    criteria (several fields: their values joined with "|").
//  * Find Unmatched: the records of a table with no match in another, a
//    left join with "Is Null" on the other side.
//  * Every design returned compiles (compileQuery) or comes with the error.

#include "base_query.h"

namespace QindaOffice::Base {

struct WizardResult {
    bool ok = false;
    QString error;
    QueryDesign design;
};

struct SimpleQuerySpec {
    QList<QPair<QString, QString>> fields; // (table, field), in order
    bool summary = false;
    // Summary: numeric field (as "table.field") → sum | average | min | max.
    QList<QPair<QString, QString>> totals;
    bool countRecords = false;          // Count Of <first table>
    QString dateGrouping = QStringLiteral("unique"); // unique | day | month | quarter | year
};

struct CrosstabWizardSpec {
    QString table;
    QStringList rowHeadings;            // up to three
    QString columnHeading;
    QString dateInterval = QStringLiteral("year"); // a date heading: year | quarter | month | date | dateTime
    QString valueField;
    QString function = QStringLiteral("sum");      // sum | average | count | min | max
    bool rowSums = true;
};

struct DuplicatesSpec {
    QString table;
    QStringList duplicateFields;
    QStringList otherFields;
};

struct UnmatchedSpec {
    QString table;        // whose records are listed
    QString relatedTable; // where a match is looked for
    QString field;        // table's matching field
    QString relatedField; // relatedTable's
    QStringList showFields;
};

[[nodiscard]] WizardResult simpleQueryWizard(const BaseFile &file, const SimpleQuerySpec &spec);
[[nodiscard]] WizardResult crosstabQueryWizard(const BaseFile &file, const CrosstabWizardSpec &spec);
[[nodiscard]] WizardResult findDuplicatesWizard(const BaseFile &file, const DuplicatesSpec &spec);
[[nodiscard]] WizardResult findUnmatchedWizard(const BaseFile &file, const UnmatchedSpec &spec);
// The joins Access would draw between `tables` (their relationships).
[[nodiscard]] QList<QueryJoin> relationshipJoins(const BaseFile &file, const QStringList &tables);

} // namespace QindaOffice::Base
