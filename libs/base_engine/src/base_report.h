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
//  * M86 (D-094): Access's Group, Sort and Total beyond the toolkit's
//    design lives in the report map's own members, which QindaTK keeps and
//    ignores: "accessGroups" (up to kReportGroupLimit levels, each with an
//    interval — prefix characters, a date part, a numeric range — an order,
//    Keep Together and its header's and footer's Force New Page and Keep
//    Together), "sections" (the report header, detail and report footer)
//    and "runningSums" (detail columns summing over the group or over the
//    report). "groups" stays the toolkit's view of the leading levels it
//    can hold (reportWithLevels writes both); when the toolkit's own
//    designer has changed "groups" since, "groups" wins (reportLevels).
//  * A group total may run (Access's Running Sum): its footer also prints
//    the total so far, over the enclosing group or over the report.
//  * Bands carry Force New Page as pageBreakBefore on the band that must
//    start a page (a page break before detail rows is a `spacer` band of its
//    own), Keep Together of a section as keepTogether, and Keep Together
//    "Whole Group" as keepWith (the header's group runs to that band); the
//    app lays them out (base_report_layout.h).

#include "base_file.h"
#include "base_query.h"

#include <QHash>
#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

constexpr int kReportRowLimit = 4000; // the toolkit's report read bound
constexpr int kReportGroupLimit = 10; // M86: Access's group levels

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
    QString running = QStringLiteral("none"); // M86: none | group | all
};

// M86: Access's Group On — the field's values grouped by an interval.
struct ReportInterval {
    // value | prefix | year | quarter | month | week | day | hour | minute | range
    QString by = QStringLiteral("value");
    double size = 1; // prefix: characters; range: its width in the field's units (money in units)
};

// M86: Access's Force New Page (none | before | after | both) and Keep
// Together of one section.
struct ReportSection {
    QString newPage = QStringLiteral("none");
    bool keepTogether = false;
    [[nodiscard]] bool before() const { return newPage == QLatin1String("before") || newPage == QLatin1String("both"); }
    [[nodiscard]] bool after() const { return newPage == QLatin1String("after") || newPage == QLatin1String("both"); }
};

struct ReportGroup {
    QString field;
    QString label; // "" = the field's label
    bool pageBreakBefore = false; // M80: every group of this level but the first starts a page
    QList<ReportTotal> totals;
    ReportInterval interval{};    // M86
    bool descending = false;      // M86: with Z on top
    QString keepTogether = QStringLiteral("no"); // M86: no | wholeGroup | withFirstDetail
    ReportSection header{};       // M86
    ReportSection footer{};       // M86
};

// M86: a detail column summing a field (or counting the records, field
// "") as the records go, over the innermost group or over the report.
struct ReportRunningSum {
    QString field;
    QString over = QStringLiteral("all"); // all | group
    QString label;
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
    // M86 (D-094): the report's own sections and running sums.
    ReportSection reportHeader;
    ReportSection detailSection;
    ReportSection reportFooter;
    QList<ReportRunningSum> runningSums;
    static ReportSpec fromReportMap(const QVariantMap &report);
};

// M86: the Group, Sort and Total levels of a report map, as the pane edits
// them ("accessGroups" while "groups" still is their view, else "groups"),
// and the report map with `levels` written back (both members).
[[nodiscard]] QVariantList reportLevels(const QVariantMap &report);
[[nodiscard]] QVariantMap reportWithLevels(const QVariantMap &report, const QVariantList &levels);

struct ReportBand {
    // One for one with QindaTK's LocalReport::Band::Kind.
    enum class Kind { ReportHeader, ReportInfo, GroupHeader, Detail, GroupFooter, ReportFooter, Empty };
    Kind kind = Kind::Detail;
    int level = -1;
    QString text;
    QStringList cells;
    bool pageBreakBefore = false;
    bool strong = false;
    // M86: Keep Together of the band's section; a spacer is an empty band
    // carrying a page break before detail rows; keepWith (a group header
    // whose group is kept whole) is the index of the band the group ends at.
    bool keepTogether = false;
    bool spacer = false;
    int keepWith = -1;
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
