// SPDX-License-Identifier: GPL-3.0-or-later
// M86 (D-094) through BaseService over the seeded Donors & Donations
// starter, against SQL worked out by hand: the four query wizards; a union
// query parsed, run and saved; calculated, value-list, hyperlink, large
// number and zero-length rules where people type; data macros saved, run
// by every writer, logged and undone; Access's XML export imported into a
// new database; tables linked to another .qbase, a CSV file and an Excel
// sheet — read-only, refreshed (undoably), converted, broken and refreshed
// again when the database opens; Group On levels past the toolkit's three;
// Force New Page and Keep Together on the pages; the AutoExec macro.

#include "base_app_test_support.h"

#include <qindatk/rich_text_codec.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_layout.h>

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QUndoStack>

using namespace BaseAppTest;

namespace {

struct Fixture {
    QTemporaryDir dir;
    BaseDocument document;
    BaseService service{&document};
    QString error;
    explicit Fixture(bool seeded = true)
    {
        if (document.createAt(dir.filePath(str("club.qbase")), seeded ? str("donors") : QString(), &error) && seeded) {
            seedDonors(*service.file());
        }
    }
    [[nodiscard]] QVariant scalar(const QString &sql) const { return service.file()->db().scalar(sql); }
    bool exec(const QString &sql) const { return service.file()->db().exec(sql).ok; }
    [[nodiscard]] QUrl url(const char *name) const { return QUrl::fromLocalFile(dir.filePath(str(name))); }
    [[nodiscard]] QUndoStack *undo() const { return document.undoStack(); }
    [[nodiscard]] QVariantMap run(const QVariantMap &design) { return service.runQuery(QString(), design, {}); }
};

QStringList names(const QVariantList &list, const char *key = "name")
{
    QStringList out;
    for (const QVariant &v : list) {
        out.append(v.toMap().value(str(key)).toString());
    }
    return out;
}

QString money(qint64 minor)
{
    return QindaOffice::Base::formatMoney(minor, 2, str("$"));
}

QVariantMap step(const char *action, const QVariantMap &arguments)
{
    return {{str("action"), str(action)}, {str("arguments"), arguments}};
}

QVariantMap macro(const QVariantList &steps)
{
    return {{str("description"), QString()}, {str("actions"), steps}};
}

QString errorOf(const QVariantMap &result)
{
    return result.value(str("error")).toString();
}

// The design of `table` with `fields` added.
QVariantMap withFields(const BaseService &service, const char *table, const QVariantList &fields)
{
    QVariantMap design = service.tableDesign(str(table));
    QVariantList all = design.value(str("fields")).toList();
    all.append(fields);
    design.insert(str("fields"), all);
    return design;
}

} // namespace

class BaseM86Test final : public QObject {
    Q_OBJECT
private slots:
    void queryWizardsMatchTheirSql()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        DatasheetModel *sheet = f.service.datasheet();
        // Find Duplicates: the cities two or more donors share, with how many.
        QVariantMap w = f.service.queryWizard(str("duplicates"), {{str("table"), str("donors")},
                                                                  {str("duplicateFields"), QStringList{str("city")}}});
        QVERIFY2(ok(w), qPrintable(errorOf(w)));
        QVERIFY2(ok(f.run(w.value(str("design")).toMap())), qPrintable(sheet->error()));
        QCOMPARE(sheet->recordCount(),
                 f.scalar(str("SELECT count(*) FROM (SELECT city FROM donors GROUP BY city HAVING count(*) > 1)")).toInt());
        QCOMPARE(names(sheet->columns()), (QStringList{str("city"), str("NumberOfDups")}));
        // Find Unmatched: donors who never gave.
        w = f.service.queryWizard(str("unmatched"), {{str("table"), str("donors")}, {str("relatedTable"), str("donations")},
                                                     {str("field"), str("id")}, {str("relatedField"), str("donor_id")},
                                                     {str("showFields"), QStringList{str("id"), str("name")}}});
        QVERIFY2(ok(w), qPrintable(errorOf(w)));
        QVERIFY(ok(f.run(w.value(str("design")).toMap())));
        QCOMPARE(sheet->recordCount(),
                 f.scalar(str("SELECT count(*) FROM donors WHERE id NOT IN (SELECT donor_id FROM donations)")).toInt());
        // Crosstab: methods down, years across, the row's sum after the headings.
        w = f.service.queryWizard(str("crosstab"), {{str("table"), str("donations")}, {str("rowHeadings"), QStringList{str("method")}},
                                                    {str("columnHeading"), str("given")}, {str("dateInterval"), str("year")},
                                                    {str("valueField"), str("amount")}, {str("function"), str("sum")},
                                                    {str("rowSums"), true}});
        QVERIFY2(ok(w), qPrintable(errorOf(w)));
        QVERIFY(ok(f.run(w.value(str("design")).toMap())));
        QCOMPARE(sheet->recordCount(), 4);
        QCOMPARE(names(sheet->columns()), (QStringList{str("method"), str("Total Of amount"), str("2025"), str("2026")}));
        QCOMPARE(sheet->cellText(str("r4"), str("2026")),
                 money(f.scalar(str("SELECT sum(amount) FROM donations WHERE method = 'Check' AND given LIKE '2026%'")).toLongLong()));
        QCOMPARE(sheet->cellText(str("r4"), str("Total Of amount")),
                 money(f.scalar(str("SELECT sum(amount) FROM donations WHERE method = 'Check'")).toLongLong()));
        // Simple Query, summarised by donor: every gift counted once.
        w = f.service.queryWizard(str("simple"),
                                  {{str("fields"), QStringList{str("donors.name"), str("donations.amount")}}, {str("summary"), true},
                                   {str("totals"), QVariantList{QVariantMap{{str("field"), str("donations.amount")}, {str("function"), str("sum")}}}},
                                   {str("countRecords"), true}});
        QVERIFY2(ok(w), qPrintable(errorOf(w)));
        const QVariantMap design = w.value(str("design")).toMap();
        QVERIFY(ok(f.run(design)));
        QCOMPARE(names(sheet->columns()), (QStringList{str("name"), str("Sum Of amount"), str("Count Of donors")}));
        QCOMPARE(sheet->recordCount(), f.scalar(str("SELECT count(DISTINCT donor_id) FROM donations")).toInt());
        const QString sql = f.service.compileQuery(design).value(str("sql")).toString();
        QCOMPARE(f.scalar(str("SELECT sum(\"Sum Of amount\") FROM (%1)").arg(sql)).toLongLong(),
                 f.scalar(str("SELECT sum(amount) FROM donations")).toLongLong());
        QCOMPARE(f.scalar(str("SELECT sum(\"Count Of donors\") FROM (%1)").arg(sql)).toLongLong(), qint64(120));
    }

    void unionQueriesParseRunAndSave()
    {
        Fixture f;
        const QVariantMap parsed = f.service.parseAccessSql(str("SELECT city FROM donors UNION SELECT method FROM donations ORDER BY city;"));
        QVERIFY2(ok(parsed), qPrintable(errorOf(parsed)));
        const QVariantMap design = parsed.value(str("design")).toMap();
        QCOMPARE(design.value(str("kind")).toString(), str("union"));
        QVERIFY(ok(f.run(design)));
        QCOMPARE(f.service.datasheet()->recordCount(),
                 f.scalar(str("SELECT count(*) FROM (SELECT city FROM donors UNION SELECT method FROM donations)")).toInt());
        QVERIFY(f.service.accessSql(design).value(str("sql")).toString().contains(str("\nUNION\n")));
        QVERIFY(ok(f.service.saveQuery(str("Places and Methods"), design)));
        QCOMPARE(f.service.queryDesign(str("Places and Methods")).value(str("kind")).toString(), str("union"));
        const QVariantMap all = f.service.parseAccessSql(str("SELECT city FROM donors UNION ALL SELECT method FROM donations;"));
        QVERIFY(ok(f.run(all.value(str("design")).toMap())));
        QCOMPARE(f.service.datasheet()->recordCount(), 160);
    }

    void fieldKindsWherePeopleType()
    {
        Fixture f;
        QVariantMap design = withFields(
            f.service, "donors",
            {QVariantMap{{str("name"), str("website")}, {str("type"), str("hyperlink")}},
             QVariantMap{{str("name"), str("points")}, {str("type"), str("largeNumber")}},
             QVariantMap{{str("name"), str("tier")}, {str("type"), str("text")},
                         {str("lookupValues"), QStringList{str("Gold"), str("Silver"), str("Bronze")}}, {str("limitToList"), true}},
             QVariantMap{{str("name"), str("nickname")}, {str("type"), str("text")}, {str("allowZeroLength"), false}}});
        QVariantMap applied = f.service.applyTableDesign(design);
        QVERIFY2(ok(applied), qPrintable(errorOf(applied)));
        design = withFields(f.service, "donations",
                            {QVariantMap{{str("name"), str("with_fee")}, {str("type"), str("calculated")},
                                         {str("expression"), str("[amount] + 100")}}});
        applied = f.service.applyTableDesign(design);
        QVERIFY2(ok(applied), qPrintable(errorOf(applied)));
        QCOMPARE(f.service.tableDesign(str("donations")).value(str("fields")).toList().last().toMap().value(str("resultType")).toString(),
                 str("currency"));
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("donations")));
        // $100.00 more than each gift, read-only.
        QCOMPARE(sheet->cellText(str("r1"), str("with_fee")),
                 money(f.scalar(str("SELECT amount FROM donations WHERE id = 1")).toLongLong() + 10000));
        QVERIFY(errorOf(sheet->setCell(str("r1"), str("with_fee"), str("1"))).contains(str("calculated")));
        QVERIFY(sheet->openTable(str("donors")));
        // A hyperlink shows its text and keeps all four parts.
        const QString link = f.service.hyperlinkText({{str("display"), str("Ada's page")}, {str("address"), str("https://example.org/ada")},
                                                      {str("subAddress"), str("bio")}});
        QCOMPARE(link, str("Ada's page#https://example.org/ada#bio"));
        QVERIFY(ok(sheet->setCell(str("r1"), str("website"), link)));
        QCOMPARE(sheet->cellText(str("r1"), str("website")), str("Ada's page"));
        QCOMPARE(sheet->cellEditText(str("r1"), str("website")), link);
        QCOMPARE(f.service.hyperlinkParts(link).value(str("target")).toString(), str("https://example.org/ada#bio"));
        // Large numbers past a double's exact range stay exact.
        QVERIFY(ok(sheet->setCell(str("r1"), str("points"), str("9007199254740993"))));
        QCOMPARE(f.scalar(str("SELECT points FROM donors WHERE id = 1")).toLongLong(), Q_INT64_C(9007199254740993));
        // A value list: its own spelling, and nothing else.
        QCOMPARE(names(sheet->lookupChoices(str("tier")), "text"), (QStringList{str("Gold"), str("Silver"), str("Bronze")}));
        QVERIFY(ok(sheet->setCell(str("r1"), str("tier"), str("silver"))));
        QCOMPARE(f.scalar(str("SELECT tier FROM donors WHERE id = 1")).toString(), str("Silver"));
        QVERIFY(errorOf(sheet->setCell(str("r1"), str("tier"), str("Platinum"))).contains(str("must be one of its list: Gold, Silver, Bronze")));
        // Allow Zero Length No: "" is refused by every writer.
        QVERIFY(!f.exec(str("UPDATE donors SET nickname = '' WHERE id = 2")));
        QVERIFY(ok(sheet->setCell(str("r2"), str("nickname"), str("Gracie"))));
    }

    void dataMacrosRunForEveryWriterAndUndo()
    {
        Fixture f;
        // Before Change refuses small gifts; After Insert logs each new one.
        const QVariantMap guard = macro({step("raiseError", {{str("description"), str("A gift is more than $1.00.")},
                                                             {str("condition"), str("[amount] <= 1")}})});
        QVERIFY(ok(f.service.checkDataMacro(str("donations"), str("beforeChange"), guard)));
        const QVariantMap window = f.service.checkDataMacro(str("donations"), str("afterInsert"),
                                                            macro({step("openForm", {{str("form"), str("Donor Entry")}})}));
        QVERIFY(!ok(window));
        QVERIFY(errorOf(window).contains(str("window actions belong to ordinary macros")));
        const int steps = f.undo()->count();
        QVERIFY2(ok(f.service.saveDataMacro(str("donations"), str("beforeChange"), guard)), "beforeChange");
        QVERIFY(ok(f.service.saveDataMacro(str("donations"), str("afterInsert"),
                                           macro({step("logEvent", {{str("description"), str("=\"New gift by \" & [method]")}})}))));
        QCOMPARE(f.undo()->count(), steps + 2);
        QCOMPARE(f.service.dataMacroDesign(str("donations"), str("afterInsert")).value(str("actions")).toList().size(), 1);
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("donations")));
        QCOMPARE(errorOf(sheet->setCell(str("r1"), str("amount"), str("0.50"))), str("A gift is more than $1.00."));
        QVERIFY(!f.exec(str("UPDATE donations SET amount = 1 WHERE id = 1"))); // another writer, the same rule
        // A new record through the datasheet: saved once its required fields are in.
        QVERIFY(ok(sheet->setCell(str("new"), str("method"), str("Cash"))));
        QVERIFY(ok(sheet->setCell(str("new"), str("donor_id"), f.scalar(str("SELECT name FROM donors WHERE id = 1")))));
        QVERIFY(ok(sheet->setCell(str("new"), str("amount"), str("12.50"))));
        QCOMPARE(f.scalar(str("SELECT count(*) FROM donations")).toInt(), 121);
        QCOMPARE(f.scalar(str("SELECT count(*) FROM qb_application_log")).toInt(), 1);
        QCOMPARE(f.scalar(str("SELECT description FROM qb_application_log")).toString(), str("New gift by Cash"));
        QVERIFY(ok(f.service.openApplicationLog()));
        QCOMPARE(sheet->recordCount(), 1);
        QCOMPARE(sheet->cellText(str("r1"), str("Event")), str("After Insert"));
        // Undo takes the record and its log line back, once; nothing re-runs.
        f.undo()->undo();
        QCOMPARE(f.scalar(str("SELECT count(*) FROM qb_application_log")).toInt(), 0);
        QCOMPARE(f.scalar(str("SELECT count(*) FROM donations")).toInt(), 120);
        f.undo()->redo();
        QCOMPARE(f.scalar(str("SELECT count(*) FROM qb_application_log")).toInt(), 1);
        QCOMPARE(f.scalar(str("SELECT count(*) FROM qb_meta WHERE key LIKE 'qb.%'")).toInt(), 0);
        // Undoing the saves takes the triggers away.
        while (f.undo()->index() > steps) {
            f.undo()->undo();
        }
        QCOMPARE(f.scalar(str("SELECT count(*) FROM sqlite_schema WHERE type = 'trigger' AND name LIKE 'qbm:%'")).toInt(), 0);
        QVERIFY(f.exec(str("UPDATE donations SET amount = 1 WHERE id = 1")));
    }

    void accessXmlImportsWhatTheExportWrote()
    {
        Fixture f;
        QVERIFY(ok(f.service.exportObject(str("table"), str("donors"), f.url("donors.xml"), str("xml"))));
        Fixture empty(false);
        QVERIFY2(empty.error.isEmpty(), qPrintable(empty.error));
        const QVariantMap tables = empty.service.xmlTables(f.url("donors.xml"));
        QVERIFY2(ok(tables), qPrintable(errorOf(tables)));
        QCOMPARE(tables.value(str("schema")).toString(), str("donors.xsd"));
        QCOMPARE(names(tables.value(str("tables")).toList()), QStringList{str("donors")});
        QCOMPARE(tables.value(str("tables")).toList().first().toMap().value(str("records")).toInt(), 40);
        const QVariantMap report = empty.service.importXml(f.url("donors.xml"), {str("donors")});
        QVERIFY2(ok(report), qPrintable(errorOf(report)));
        QCOMPARE(report.value(str("tables")).toList().first().toMap().value(str("rows")).toInt(), 40);
        // The same records, typed as they were: keys, e-mails and dates.
        for (const char *sql : {"SELECT group_concat(id || '|' || name || '|' || coalesce(email, '') || '|' || first_gift, ';') "
                                "FROM (SELECT * FROM donors ORDER BY id)",
                                "SELECT typeof(first_gift) || typeof(id) FROM donors LIMIT 1"}) {
            QCOMPARE(empty.scalar(str(sql)), f.scalar(str(sql)));
        }
        QCOMPARE(empty.service.tableDesign(str("donors")).value(str("fields")).toList().first().toMap().value(str("type")).toString(),
                 str("autoNumber"));
        empty.undo()->undo();
        QVERIFY(!empty.service.tableNames().contains(str("donors")));
    }

    void linksToADatabaseATextFileAndASheet()
    {
        Fixture f;
        BaseDocument source;
        QString error;
        QVERIFY(source.createAt(f.dir.filePath(str("members.qbase")), str("donors"), &error));
        QVERIFY(seedDonors(*source.file()));
        const QVariantMap choices = f.service.linkChoices(f.url("members.qbase"), str("qbase"));
        QVERIFY(choices.value(str("tables")).toStringList().contains(str("donors")));
        QVariantMap linked = f.service.linkTable(str("members"), {{str("kind"), str("qbase")}, {str("url"), f.url("members.qbase")},
                                                                  {str("table"), str("donors")}});
        QVERIFY2(ok(linked), qPrintable(errorOf(linked)));
        QCOMPARE(linked.value(str("rows")).toInt(), 40);
        QVERIFY(f.service.linkInfo(str("members")).value(str("linked")).toBool());
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("members")));
        QVERIFY(!sheet->editable());
        QVERIFY(!f.exec(str("UPDATE members SET city = 'Bend'"))); // the guard refuses every writer
        // The source changes; Refresh reads it again as one undo step.
        QVERIFY(source.file()->db().exec(str("UPDATE donors SET city = 'Bend' WHERE id = 1")).ok);
        const QString city = f.scalar(str("SELECT city FROM members WHERE id = 1")).toString();
        QVERIFY(ok(f.service.refreshLink(str("members"))));
        QCOMPARE(f.scalar(str("SELECT city FROM members WHERE id = 1")).toString(), str("Bend"));
        f.undo()->undo();
        QCOMPARE(f.scalar(str("SELECT city FROM members WHERE id = 1")).toString(), city);
        // A CSV file and an Excel sheet.
        QFile csv(f.dir.filePath(str("prices.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("item,price\nPen,1.99\nInk,4.50\n");
        csv.close();
        linked = f.service.linkTable(str("prices"), {{str("kind"), str("csv")}, {str("url"), f.url("prices.csv")}, {str("headerRow"), true}});
        QVERIFY2(ok(linked), qPrintable(errorOf(linked)));
        QCOMPARE(linked.value(str("rows")).toInt(), 2);
        QVERIFY(ok(f.service.exportObject(str("table"), str("campaigns"), f.url("campaigns.xlsx"), str("xlsx"))));
        const QStringList sheets = f.service.linkChoices(f.url("campaigns.xlsx"), str("xlsx")).value(str("sheets")).toStringList();
        QVERIFY(!sheets.isEmpty());
        linked = f.service.linkTable(str("goals"), {{str("kind"), str("xlsx")}, {str("url"), f.url("campaigns.xlsx")},
                                                    {str("sheet"), sheets.first()}, {str("headerRow"), true}});
        QVERIFY2(ok(linked), qPrintable(errorOf(linked)));
        QCOMPARE(linked.value(str("rows")).toInt(), 4);
        QCOMPARE(names(f.service.linkedTables(), "table"), (QStringList{str("goals"), str("members"), str("prices")}));
        // Refresh All Links follows a longer file.
        QVERIFY(csv.open(QIODevice::Append));
        csv.write("Nib,0.25\n");
        csv.close();
        const QVariantMap all = f.service.refreshAllLinks();
        QVERIFY2(ok(all), qPrintable(errorOf(all)));
        QCOMPARE(all.value(str("refreshed")).toInt(), 3);
        QCOMPARE(f.scalar(str("SELECT count(*) FROM prices")).toInt(), 3);
        // Convert to a local table: editable; undo links it again.
        QVERIFY(ok(f.service.unlinkTable(str("prices"))));
        QVERIFY(!f.service.linkInfo(str("prices")).value(str("linked")).toBool());
        QVERIFY(f.exec(str("UPDATE prices SET item = 'Quill' WHERE item = 'Nib'")));
        QVERIFY(f.exec(str("UPDATE prices SET item = 'Nib' WHERE item = 'Quill'")));
        f.undo()->undo();
        QVERIFY(f.service.linkInfo(str("prices")).value(str("linked")).toBool());
        // A source that is gone: the copy stays and says why.
        QVERIFY(QFile::remove(f.dir.filePath(str("prices.csv"))));
        QVERIFY(!ok(f.service.refreshLink(str("prices"))));
        QCOMPARE(f.scalar(str("SELECT count(*) FROM prices")).toInt(), 3);
        QVERIFY(!f.service.linkInfo(str("prices")).value(str("problem")).toString().isEmpty());
        // Opening the database reads every link again.
        BaseDocument reopened;
        BaseService service(&reopened);
        QSignalSpy opened(&service, &BaseService::databaseOpened);
        QVERIFY(reopened.load(f.dir.filePath(str("club.qbase")), &error));
        QCOMPARE(opened.count(), 1);
        QVariantMap results;
        for (const QVariant &v : service.linkRefreshResults()) {
            results.insert(v.toMap().value(str("table")).toString(), v.toMap().value(str("ok")));
        }
        QCOMPARE(results, (QVariantMap{{str("goals"), true}, {str("members"), true}, {str("prices"), false}}));
        QCOMPARE(service.file()->db().scalar(str("SELECT city FROM members WHERE id = 1")).toString(), str("Bend"));
    }

    void reportLevelsAndPageControl()
    {
        Fixture f;
        QVERIFY(ok(f.service.openReport(str("Donations by Donor"), {})));
        const QVariantMap base{{str("tableName"), str("Donation Details")}, {str("headerText"), str("Donations")}};
        const QVariantMap sum{{str("field"), str("amount")}, {str("function"), str("sum")}};
        // Four levels: the toolkit's view keeps what it can hold.
        const QVariantList four{QVariantMap{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("year")}}}},
                                QVariantMap{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("quarter")}}}},
                                QVariantMap{{str("field"), str("method")}}, QVariantMap{{str("field"), str("Donor")}}};
        QVariantMap report = f.service.reportWithLevels(base, four);
        QCOMPARE(f.service.reportLevels(report).size(), 4);
        QVERIFY(report.value(str("groups")).toList().size() <= 3);
        QindaTK::RichTextDocument pages;
        QVariantMap built = f.service.buildReport(&pages, report);
        QVERIFY2(ok(built), qPrintable(errorOf(built)));
        QCOMPARE(built.value(str("rowCount")).toInt(), 120);
        // Every donor on a page of its own: one break before each group but the first page's.
        const int donors = f.scalar(str("SELECT count(DISTINCT donor_id) FROM donations")).toInt();
        report = f.service.reportWithLevels(base, {QVariantMap{{str("field"), str("Donor")}, {str("totals"), QVariantList{sum}},
                                                               {str("header"), QVariantMap{{str("newPage"), str("before")}}}}});
        built = f.service.buildReport(&pages, report);
        QVERIFY2(ok(built), qPrintable(errorOf(built)));
        QVERIFY(built.value(str("pageControl")).toBool());
        QCOMPARE(built.value(str("groupCount")).toInt(), donors);
        int breaks = 0;
        for (const QVariant &b : built.value(str("bands")).toList()) {
            breaks += b.toMap().value(str("pageBreakBefore")).toBool() || b.toMap().value(str("spacer")).toBool() ? 1 : 0;
        }
        QCOMPARE(built.value(str("pageBreaks")).toInt(), breaks);
        QVERIFY(breaks >= donors - 1);
        int marked = 0;
        for (const QJsonValue &block : QindaTK::RichTextCodec::toJsonObject(pages).value(str("blocks")).toArray()) {
            marked += block.toObject().value(str("paragraph")).toObject().value(str("format")).toObject().value(str("pageBreakBefore")).toBool();
        }
        QCOMPARE(marked, breaks);
        // Keep Together, whole group: no donor's gifts cross a page.
        report = f.service.reportWithLevels(base, {QVariantMap{{str("field"), str("Donor")}, {str("totals"), QVariantList{sum}},
                                                               {str("keepTogether"), str("wholeGroup")}}});
        built = f.service.buildReport(&pages, report);
        QVERIFY2(ok(built), qPrintable(errorOf(built)));
        QVERIFY(built.value(str("groupsMoved")).toInt() > 0);
        QindaTK::RichTextLayout layout;
        layout.setDocument(&pages);
        layout.setMode(QindaTK::RichTextLayout::Mode::Paged);
        layout.paginateAll();
        QVERIFY(layout.pageCount() > 1);
    }

    void autoExecIsFoundByName()
    {
        Fixture f;
        QCOMPARE(f.service.autoExecMacro(), QString());
        QVERIFY(ok(f.service.saveMacro(str("autoexec"), macro({step("openTable", {{str("table"), str("donors")}})}))));
        QCOMPARE(f.service.autoExecMacro(), str("autoexec"));
    }
};

BASE_APP_TEST_MAIN(BaseM86Test)

#include "tst_qindabase_m86.moc"
