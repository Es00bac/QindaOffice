// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reports evaluated by the engine (M80, D-089 §6). A report's records come
// from a table or a saved select query (readReportSource, parameters asked
// first); runReport filters, sorts and groups them (Access's Group, Sort
// and Total) and totals every group with exact money — a currency sum is
// integer minor units — into bands. The app lays the bands out with
// QindaTK's report layout (LocalReport::documentJson), so the toolkit's
// pages, page bands, preview, print and PDF stay as they were while every
// printed number is the engine's own (QindaTK's report engine totals in
// double precision and asks hosts needing exact money for their own
// backend — docs/forms-reports.md in QindaTK).
//
// AGENT-CONTRACT (with apps/qindabase base_service_reports.cpp, which maps
// ReportRun onto QindaTK's LocalReport::Output one band for one band, and
// with the QindaTK design document's report map):
//  * ReportSpec::fromReportMap reads the toolkit's report definition:
//    headerText, footerText, pageHeaderText, pageFooterText (absent: "Page
//    {page} of {pages}"; "" turns it off), columns ([field] or [{field,
//    label}]; empty: every column), groups ([{field, label,
//    pageBreakBefore, totals:[{field, function, label}]}], outermost
//    first), totals (the report footer), detail, query.sort ([{field,
//    ascending}]), query.filter + query.parameters (the ReportDesigner's
//    equality filter) and page {paperId, orientation}. The first-cut keys
//    groupField/sumField/filterField/filterValue read as one group with a
//    sum and an equality filter, as in the toolkit.
//  * Functions are the toolkit's: countRows, countValues, sum, average,
//    minimum, maximum. Currency sums and extremes are exact minor units, a
//    currency average rounds half away from zero to the minor unit,
//    whole-number sums are exact, decimal numbers sum as doubles. Sums and
//    averages skip Nulls; a group whose values are all Null totals to "—".
//  * Rows sort by the group keys (ascending, Nulls last), then the sort
//    keys; ties keep the source's order. Lookups group, sort and print by
//    the related record's name; a Null key prints "(blank)".
//  * Band texts follow the toolkit's: a group header "Label: value", a
//    group footer "Label value total · Count n · Total amount $1,250.00",
//    the report footer "Report total · Count n · …".

#include "base_file.h"
#include "base_query.h"

#include <QHash>
#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

constexpr int kReportRowLimit = 4000; // the toolkit's report read bound

struct ReportColumn {
    QString name;
    QString label;     // the caption (tables) or the output name (queries)
    FieldType type = FieldType::Text;
    QHash<QString, QString> lookup; // stored value (as text) → the related record's name
    QString inputMask;              // text shown through its mask
};

struct ReportSource {
    QString kind; // table | query
    QString name;
    QList<ReportColumn> columns;
    QList<QVariantList> rows; // storage values (base_types.h)
    bool truncated = false;   // more than the limit matched
    QList<QueryParameter> parameters;
    QVariantMap parameterValues;
    [[nodiscard]] int columnIndex(const QString &name) const;
    // A stored value as a report prints it (lookups as names, money with
    // its symbol, masks applied).
    [[nodiscard]] QString display(int column, const QVariant &stored, const CurrencyFormat &currency) const;
};

// The records of a table or a saved select (or crosstab) query, at most
// `limit` (truncated says when there were more). nullopt with `error` for
// a missing source, an action query or a parameter that does not parse.
[[nodiscard]] std::optional<ReportSource> readReportSource(const BaseFile &file, const QString &kind,
                                                           const QString &name, const QVariantMap &parameters,
                                                           QString *error, int limit = kReportRowLimit);

struct ReportTotal {
    QString field;
    QString function = QStringLiteral("sum");
    QString label; // "" = "Sum of <field label>"
};

struct ReportGroup {
    QString field;
    QString label; // "" = the field's label
    bool pageBreakBefore = false;
    QList<ReportTotal> totals;
};

struct ReportSortKey {
    QString field;
    bool descending = false;
};

struct ReportSpec {
    QString title;
    QString footer;
    QString pageHeader;
    QString pageFooter = QStringLiteral("Page {page} of {pages}");
    QList<QPair<QString, QString>> columns; // field, label ("" = the field's label)
    QList<ReportGroup> groups;
    QList<ReportTotal> totals; // the report footer
    QList<ReportSortKey> sort;
    bool detail = true;
    QString filterField; // the designer's equality filter
    QString filterValue;
    QString paperId = QStringLiteral("letter");
    bool landscape = false;
    static ReportSpec fromReportMap(const QVariantMap &report);
};

struct ReportBand {
    // One for one with QindaTK's LocalReport::Band::Kind.
    enum class Kind { ReportHeader, ReportInfo, GroupHeader, Detail, GroupFooter, ReportFooter, Empty };
    Kind kind = Kind::Detail;
    int level = -1;
    QString text;
    QStringList cells;
    bool pageBreakBefore = false;
    bool strong = false;
};

// Every total the run computed, in band order (level -1: the report footer).
struct ReportTotalValue {
    int level = -1;
    QStringList key; // the group values (display text), outermost first
    QString field;
    QString function;
    QVariant value;  // storage-like: qint64 minor units for money, double, text
    QString text;    // as printed
};

struct ReportRun {
    bool ok = false;
    QString error;
    QStringList columnLabels;
    QList<ReportBand> bands;
    QList<ReportTotalValue> totals;
    int detailRows = 0;
    int groupCount = 0; // groups of the innermost level
};

[[nodiscard]] ReportRun runReport(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency);

} // namespace QindaOffice::Base
