// SPDX-License-Identifier: GPL-3.0-or-later
// M86's report grouping (D-094) against hand-worked results: Access's Group
// On intervals (prefix characters, month, quarter, year, money ranges),
// four group levels, descending groups, running sums over a group and over
// the report (detail columns and group totals), Force New Page and Keep
// Together carried on the bands, and the report map's levels kept beside
// the toolkit's three-level view.
//
// Seven gifts: Ada Card $10.10 2026-01-02 · Ada Card $20.20 2026-01-05 ·
// Ada Check $0.01 2026-02-01 · Grace Card $100.00 2026-01-10 · Grace Cash
// (no amount) 2026-03-01 · Alan Check $33.33 2026-02-02 · Ann Card $5.00
// 2025-11-20.

#include "base_fixture.h"

#include "base_report.h"

using namespace BaseTest;

namespace {

struct Gifts : Db {
    std::optional<ReportSource> source;
    Gifts()
    {
        create(table("gifts", {field("id", FieldType::AutoNumber), field("donor", FieldType::Text),
                               field("method", FieldType::Text), field("amount", FieldType::Currency),
                               field("given", FieldType::Date)}));
        exec(str("INSERT INTO gifts(donor, method, amount, given) VALUES"
                 "('Ada', 'Card', 1010, '2026-01-02'), ('Ada', 'Card', 2020, '2026-01-05'),"
                 "('Ada', 'Check', 1, '2026-02-01'), ('Grace', 'Card', 10000, '2026-01-10'),"
                 "('Grace', 'Cash', NULL, '2026-03-01'), ('Alan', 'Check', 3333, '2026-02-02'),"
                 "('Ann', 'Card', 500, '2025-11-20')"));
        QString error;
        source = readReportSource(*file, str("table"), str("gifts"), {}, &error);
    }
};

ReportGroup group(const char *field, const char *by = "value", double size = 1)
{
    ReportGroup g;
    g.field = str(field);
    g.interval.by = str(by);
    g.interval.size = size;
    g.totals = {{str("amount"), str("sum"), QString()}};
    return g;
}

QStringList texts(const ReportRun &run, ReportBand::Kind kind)
{
    QStringList out;
    for (const ReportBand &b : run.bands) {
        if (b.kind == kind) {
            out.append(b.kind == ReportBand::Kind::Detail ? b.cells.join(QLatin1Char('|'))
                       : b.kind == ReportBand::Kind::GroupHeader ? QStringLiteral("%1:%2").arg(b.level).arg(b.text)
                                                                 : b.text);
        }
    }
    return out;
}

ReportRun runWith(const Gifts &db, const ReportSpec &spec)
{
    return runReport(*db.source, spec, db.file->currency());
}

} // namespace

class BaseReportM86Test final : public QObject {
    Q_OBJECT
private slots:
    void groupsOnIntervals()
    {
        Gifts db;
        QVERIFY(db.source);
        ReportSpec spec;
        spec.columns = {{str("donor"), QString()}, {str("amount"), QString()}};
        spec.groups = {group("donor", "prefix", 1)};
        ReportRun run = runWith(db, spec);
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(texts(run, ReportBand::Kind::GroupHeader), (QStringList{str("0:donor: A"), str("0:donor: G")}));
        QVERIFY(texts(run, ReportBand::Kind::GroupFooter).first().endsWith(str("Sum of amount $68.64")));
        spec.groups = {group("given", "month")};
        run = runWith(db, spec);
        QCOMPARE(texts(run, ReportBand::Kind::GroupHeader),
                 (QStringList{str("0:given: November 2025"), str("0:given: January 2026"), str("0:given: February 2026"),
                              str("0:given: March 2026")}));
        const QStringList months = texts(run, ReportBand::Kind::GroupFooter);
        QVERIFY(months.at(1).endsWith(str("Count 3 · Sum of amount $130.30")));
        QVERIFY(months.at(3).endsWith(str("Sum of amount —"))); // only a gift with no amount
        spec.groups = {group("given", "quarter")};
        QCOMPARE(texts(runWith(db, spec), ReportBand::Kind::GroupHeader),
                 (QStringList{str("0:given: Q4 2025"), str("0:given: Q1 2026")}));
        ReportGroup years = group("given", "year");
        years.descending = true;
        spec.groups = {years};
        run = runWith(db, spec);
        QCOMPARE(texts(run, ReportBand::Kind::GroupHeader), (QStringList{str("0:given: 2026"), str("0:given: 2025")}));
        QVERIFY(texts(run, ReportBand::Kind::GroupFooter).first().endsWith(str("Sum of amount $163.64")));
        spec.groups = {group("amount", "range", 50)};
        run = runWith(db, spec);
        QCOMPARE(texts(run, ReportBand::Kind::GroupHeader),
                 (QStringList{str("0:amount: $0.00 – $49.99"), str("0:amount: $100.00 – $149.99"),
                              str("0:amount: (blank)")}));
        QVERIFY(texts(run, ReportBand::Kind::GroupFooter).first().endsWith(str("Count 5 · Sum of amount $68.64")));
        // Intervals fit their fields.
        spec.groups = {group("donor", "month")};
        QVERIFY(runWith(db, spec).error.contains(str("not a date")));
        spec.groups = {group("given", "range", 10)};
        QVERIFY(runWith(db, spec).error.contains(str("not a number")));
        spec.groups = {group("amount", "prefix", 1)};
        QVERIFY(runWith(db, spec).error.contains(str("not text")));
    }

    void fourLevelsNest()
    {
        Gifts db;
        ReportSpec spec;
        spec.columns = {{str("amount"), QString()}};
        spec.groups = {group("given", "year"), group("given", "month"), group("method"), group("donor")};
        const ReportRun run = runWith(db, spec);
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(texts(run, ReportBand::Kind::GroupHeader),
                 (QStringList{str("0:given: 2025"), str("1:given: November 2025"), str("2:method: Card"), str("3:donor: Ann"),
                              str("0:given: 2026"), str("1:given: January 2026"), str("2:method: Card"), str("3:donor: Ada"),
                              str("3:donor: Grace"), str("1:given: February 2026"), str("2:method: Check"),
                              str("3:donor: Ada"), str("3:donor: Alan"), str("1:given: March 2026"), str("2:method: Cash"),
                              str("3:donor: Grace")}));
        QCOMPARE(run.groupCount, 6);
        QCOMPARE(run.detailRows, 7);
        spec.groups = QList<ReportGroup>(kReportGroupLimit + 1, group("donor"));
        QVERIFY(runWith(db, spec).error.contains(str("at most 10")));
    }

    void runningSums()
    {
        Gifts db;
        ReportSpec spec;
        spec.columns = {{str("given"), QString()}, {str("amount"), QString()}};
        ReportGroup donor = group("donor");
        donor.totals.first().running = str("all");
        spec.groups = {donor};
        spec.sort = {{str("given"), false}};
        spec.runningSums = {{str("amount"), str("group"), QString()}, {QString(), str("all"), QString()}};
        const ReportRun run = runWith(db, spec);
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(run.columnLabels, (QStringList{str("given"), str("amount"), str("Running sum of amount"), str("No.")}));
        QCOMPARE(texts(run, ReportBand::Kind::Detail),
                 (QStringList{str("2026-01-02|$10.10|$10.10|1"), str("2026-01-05|$20.20|$30.30|2"),
                              str("2026-02-01|$0.01|$30.31|3"), str("2026-02-02|$33.33|$33.33|4"),
                              str("2025-11-20|$5.00|$5.00|5"), str("2026-01-10|$100.00|$100.00|6"),
                              str("2026-03-01||$100.00|7")}));
        const QStringList footers = texts(run, ReportBand::Kind::GroupFooter);
        QCOMPARE(footers.at(2), str("donor Ann total · Count 1 · Sum of amount $5.00 · Running Sum of amount $68.64"));
        QCOMPARE(footers.at(3), str("donor Grace total · Count 2 · Sum of amount $100.00 · Running Sum of amount $168.64"));
        // A running average makes no sense: refused.
        donor.totals = {{str("amount"), str("average"), QString(), str("all")}};
        spec.groups = {donor};
        QVERIFY(runWith(db, spec).error.contains(str("running total adds sums or counts")));
    }

    void pagesBreakAndKeepTogether()
    {
        Gifts db;
        ReportSpec spec;
        spec.columns = {{str("amount"), QString()}};
        spec.totals = {{str("amount"), str("sum"), QString()}};
        ReportGroup method = group("method");
        method.footer.newPage = str("after");
        method.footer.keepTogether = true;
        method.keepTogether = str("wholeGroup");
        spec.groups = {method};
        spec.reportHeader.newPage = str("after");
        ReportRun run = runWith(db, spec);
        QVERIFY2(run.ok, qPrintable(run.error));
        QList<int> breaks;
        for (int i = 0; i < run.bands.size(); ++i) {
            if (run.bands.at(i).pageBreakBefore) {
                breaks.append(i);
            }
        }
        // The first group starts page 2 (after the title page); every band
        // after a group footer starts a page, the report total included.
        QStringList broken;
        for (const int i : std::as_const(breaks)) {
            broken.append(run.bands.at(i).kind == ReportBand::Kind::GroupHeader ? run.bands.at(i).text : str("total"));
        }
        QCOMPARE(broken, (QStringList{str("method: Card"), str("method: Cash"), str("method: Check"), str("total")}));
        for (int i = 0; i < run.bands.size(); ++i) {
            const ReportBand &b = run.bands.at(i);
            if (b.kind == ReportBand::Kind::GroupFooter) {
                QVERIFY(b.keepTogether);
            }
            if (b.kind == ReportBand::Kind::GroupHeader) {
                QVERIFY(b.keepWith > i);
                QCOMPARE(run.bands.at(b.keepWith).kind, ReportBand::Kind::GroupFooter);
                QCOMPARE(run.bands.at(b.keepWith).level, 0);
            }
        }
        // Detail on a page of its own: a spacer before every record but the
        // first under its heading.
        spec.reportHeader = {};
        spec.groups = {group("method")};
        spec.groups.first().totals.clear();
        spec.detailSection.newPage = str("before");
        run = runWith(db, spec);
        int spacers = 0;
        for (int i = 0; i < run.bands.size(); ++i) {
            if (run.bands.at(i).spacer) {
                ++spacers;
                QVERIFY(run.bands.at(i).pageBreakBefore);
                QCOMPARE(run.bands.at(i + 1).kind, ReportBand::Kind::Detail);
            }
        }
        QCOMPARE(spacers, 4); // Card has 4 gifts, Cash 1, Check 2: 3 + 0 + 1
        spec.detailSection = {};
        spec.reportFooter.newPage = str("before");
        run = runWith(db, spec);
        QVERIFY(run.bands.at(run.bands.size() - 1).pageBreakBefore);
    }

    void levelsBesideTheToolkitsGroups()
    {
        const QVariantMap year{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("year")}}}};
        const QVariantMap month{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("month")}}}};
        const QVariantMap method{{str("field"), str("method")},
                                 {str("header"), QVariantMap{{str("newPage"), str("before")}}},
                                 {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")},
                                                                          {str("function"), str("sum")},
                                                                          {str("running"), str("all")}}}}};
        const QVariantList levels{year, month, method, QVariantMap{{str("field"), str("donor")}}};
        const QVariantMap report = reportWithLevels({{str("headerText"), str("Gifts")}}, levels);
        // The toolkit sees what it can hold: the first level (the second
        // repeats its field); M86's options stay out of its view.
        const QVariantList groups = report.value(str("groups")).toList();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.first().toMap().value(str("field")).toString(), str("given"));
        QVERIFY(!groups.first().toMap().contains(str("interval")));
        QCOMPARE(reportLevels(report), levels);
        const ReportSpec spec = ReportSpec::fromReportMap(report);
        QCOMPARE(spec.groups.size(), 4);
        QCOMPARE(spec.groups.at(1).interval.by, str("month"));
        QVERIFY(spec.groups.at(2).header.before());
        QCOMPARE(spec.groups.at(2).totals.first().running, str("all"));
        // The toolkit's own designer changed the groups since: its groups
        // win, a level on the same field keeping its options.
        QVariantMap edited = report;
        edited.insert(str("groups"), QVariantList{QVariantMap{{str("field"), str("method")}}});
        const QVariantList now = reportLevels(edited);
        QCOMPARE(now.size(), 1);
        QCOMPARE(now.first().toMap().value(str("header")).toMap().value(str("newPage")).toString(), str("before"));
        // Three distinct plain levels: the toolkit holds them all; M80's
        // "a new page per group" reads as the header's Force New Page.
        const QVariantMap plain = reportWithLevels({}, {QVariantMap{{str("field"), str("a")}, {str("pageBreakBefore"), true}},
                                                        QVariantMap{{str("field"), str("b")}},
                                                        QVariantMap{{str("field"), str("c")}}, QVariantMap{{str("field"), str("d")}}});
        QCOMPARE(plain.value(str("groups")).toList().size(), 3);
        QVERIFY(ReportSpec::fromReportMap(plain).groups.first().header.before());
    }
};

BASE_TEST_MAIN(BaseReportM86Test)

#include "tst_base_report_m86.moc"
