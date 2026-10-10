// SPDX-License-Identifier: GPL-3.0-or-later
// M80 (D-089) through BaseService over the seeded Donors & Donations
// starter: the Access SQL view both ways, the template's crosstab and a
// make-table run undone through the document; Test Validation Rules,
// masks and rules where people type; macros and where conditions; form
// lookups, controls and a subform's linked datasheet; reports evaluated
// by the engine with exact totals; mailing labels; Access's XML.

#include "base_app_test_support.h"

#include "label_sheet.h"

#include <qindatk/rich_text_document.h>

#include <QFile>
#include <QUndoStack>

using namespace BaseAppTest;
using QindaOffice::Apps::Base::LabelSheet;

namespace {

struct Fixture {
    QTemporaryDir dir;
    BaseDocument document;
    BaseService service{&document};
    QString error;
    Fixture()
    {
        if (document.createAt(dir.filePath(str("club.qbase")), str("donors"), &error)) {
            seedDonors(*service.file());
        }
    }
    [[nodiscard]] QVariant scalar(const QString &sql) const { return service.file()->db().scalar(sql); }
    bool exec(const QString &sql) const { return service.file()->db().exec(sql).ok; }
    [[nodiscard]] QUrl url(const char *name) const { return QUrl::fromLocalFile(dir.filePath(str(name))); }
    [[nodiscard]] QUndoStack *undo() const { return document.undoStack(); }
};

QVariantMap column(const char *table, const char *field, const char *criteria = "")
{
    return {{str("table"), str(table)}, {str("field"), str(field)}, {str("criteria"), str(criteria)},
            {str("show"), true}, {str("total"), str("groupBy")}};
}

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

} // namespace

class BaseM80Test final : public QObject {
    Q_OBJECT
private slots:
    void accessSqlViewBothWays()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        const QVariantMap design = f.service.queryDesign(str("Totals by Donor"));
        const QString sql = f.service.accessSql(design).value(str("sql")).toString();
        QVERIFY2(sql.startsWith(str("SELECT donors.[name] AS Donor, Sum(donations.amount) AS [Total given]")), qPrintable(sql));
        const QVariantMap back = f.service.parseAccessSql(sql);
        QVERIFY2(ok(back), qPrintable(back.value(str("error")).toString()));
        QCOMPARE(f.service.compileQuery(back.value(str("design")).toMap()).value(str("sql")),
                 f.service.compileQuery(design).value(str("sql")));
        // M86 (D-094): a union is a design of its own; one whose parts show
        // different numbers of columns is still refused.
        const QVariantMap refused = f.service.parseAccessSql(str("SELECT name FROM donors UNION SELECT name, id FROM campaigns"));
        QVERIFY(!ok(refused));
        QVERIFY(refused.value(str("error")).toString().contains(str("union")));
    }

    void crosstabAndMakeTableRun()
    {
        Fixture f;
        QVERIFY(ok(f.service.runQuery(str("Donations by Method and Year"), {}, {})));
        DatasheetModel *sheet = f.service.datasheet();
        QCOMPARE(sheet->recordCount(), 4); // one row per method
        QCOMPARE(names(sheet->columns()), (QStringList{str("method"), str("2025"), str("2026")}));
        const qint64 check = f.scalar(str("SELECT sum(amount) FROM donations WHERE method = 'Check' AND given LIKE '2026%'"))
                                 .toLongLong();
        QCOMPARE(sheet->cellText(str("r4"), str("2026")), money(check)); // Check sorts last
        // A make-table query: its plan first, then one undo step.
        QVariantMap design = f.service.newQueryDesign({str("donations")});
        design.insert(str("kind"), str("makeTable"));
        design.insert(str("makeTable"), str("big_gifts"));
        design.insert(str("columns"), QVariantList{column("donations", "amount", ">= 500"), column("donations", "given")});
        const QVariantMap planned = f.service.runQuery(QString(), design, {});
        QVERIFY2(ok(planned), qPrintable(planned.value(str("error")).toString()));
        QCOMPARE(planned.value(str("makeTable")).toString(), str("big_gifts"));
        const qint64 big = f.scalar(str("SELECT count(*) FROM donations WHERE amount >= 50000")).toLongLong();
        QCOMPARE(planned.value(str("affected")).toLongLong(), big);
        QVERIFY(planned.value(str("plan")).toMap().value(str("sql")).toString().contains(str("CREATE TABLE")));
        const int steps = f.undo()->count();
        QVERIFY(ok(f.service.runMakeTable(design, {})));
        QVERIFY(f.service.tableNames().contains(str("big_gifts")));
        QCOMPARE(f.undo()->count(), steps + 1);
        f.undo()->undo();
        QVERIFY(!f.service.tableNames().contains(str("big_gifts")));
    }

    void rulesAndMasksWherePeopleType()
    {
        Fixture f;
        QVariantMap test = f.service.testValidationRules(str("donations"));
        QVERIFY(ok(test));
        QCOMPARE(test.value(str("broken")).toLongLong(), qint64(0));
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("donations")));
        const QVariantMap refused = sheet->setCell(str("r2"), str("amount"), str("0"));
        QVERIFY(!ok(refused));
        QCOMPARE(refused.value(str("error")).toString(), str("A donation is more than nothing."));
        // Another tool wrote around the rule: the test finds the record.
        QVERIFY(f.exec(str("DROP TRIGGER \"qbv:donations:amount:u\"")));
        QVERIFY(f.exec(str("UPDATE donations SET amount = -5 WHERE id = 1")));
        test = f.service.testValidationRules(str("donations"));
        QCOMPARE(test.value(str("broken")).toLongLong(), qint64(1));
        QVERIFY(test.value(str("lines")).toStringList().first().startsWith(str("1 record(s) break the validation rule of amount")));
        // The template's phone mask keeps the literals.
        QVERIFY(sheet->openTable(str("donors")));
        QVERIFY(ok(sheet->setCell(str("r1"), str("phone"), str("503 555 0142"))));
        QCOMPARE(f.scalar(str("SELECT phone FROM donors WHERE id = 1")).toString(), str("(503) 555-0142"));
        QVERIFY(!ok(sheet->setCell(str("r1"), str("phone"), str("555"))));
        QVERIFY(sheet->columns().at(3).toMap().value(str("maskHint")).toString().contains(str("___-____")));
    }

    void macrosAndWhereConditions()
    {
        Fixture f;
        QCOMPARE(f.service.macroNames(), QStringList{str("New Donation")});
        QVariantMap draft = f.service.newMacroDesign();
        draft.insert(str("actions"), QVariantList{
            QVariantMap{{str("action"), str("openForm")},
                        {str("arguments"), QVariantMap{{str("form"), str("Donor")}, {str("where"), str("[city] = \"Salem\"")}}}},
            QVariantMap{{str("action"), str("goToRecord")}, {str("arguments"), QVariantMap{{str("record"), str("last")}}}}});
        QVERIFY(ok(f.service.checkMacro(draft)));
        QVERIFY(ok(f.service.saveMacro(str("Salem Donors"), draft)));
        QVERIFY(f.service.macroNames().contains(str("Salem Donors")));
        QCOMPARE(f.service.macroDesign(str("Salem Donors")).value(str("actions")).toList().size(), 2);
        f.undo()->undo();
        QVERIFY(!f.service.macroNames().contains(str("Salem Donors")));
        QVariantMap loop = draft;
        loop.insert(str("actions"), QVariantList{QVariantMap{{str("action"), str("runMacro")},
                                                             {str("arguments"), QVariantMap{{str("macro"), str("Loop")}}}}});
        QVERIFY(!ok(f.service.saveMacro(str("Loop"), loop)));
        QVariantMap broken = draft;
        broken.insert(str("actions"), QVariantList{QVariantMap{{str("action"), str("openForm")},
                                                               {str("arguments"), QVariantMap{{str("form"), str("Nope")}}}}});
        QCOMPARE(f.service.checkMacro(broken).value(str("problems")).toStringList().size(), 1);
        const qint64 salem = f.scalar(str("SELECT count(*) FROM donors WHERE city = 'Salem'")).toLongLong();
        const QVariantMap rows = f.service.whereRowIds(str("donors"), str("[city] = \"Salem\""));
        QVERIFY(ok(rows));
        QCOMPARE(rows.value(str("rowIds")).toList().size(), salem);
        QVERIFY(!ok(f.service.whereRowIds(str("donors"), str("[town] = 1"))));
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("donors")));
        QVERIFY(ok(sheet->applyWhere(str("[city] = \"Salem\""))));
        QCOMPARE(sheet->recordCount(), int(salem));
        QVERIFY(!sheet->filters().isEmpty());
        sheet->clearFilters();
        QCOMPARE(sheet->recordCount(), 40);
    }

    void formLookupsControlsAndSubforms()
    {
        Fixture f;
        const QVariantMap entry = f.service.formControls(str("Donation Entry"));
        QVERIFY(ok(entry));
        QVariantList lookups = entry.value(str("lookups")).toList();
        QCOMPARE(names(lookups), (QStringList{str("donor_id"), str("campaign_id")}));
        QCOMPARE(lookups.first().toMap().value(str("control")).toString(), str("combo"));
        const QVariantMap donor = f.service.formControls(str("Donor"));
        QCOMPARE(names(donor.value(str("controls")).toList(), "id"), (QStringList{str("subform1"), str("button1")}));
        QCOMPARE(donor.value(str("subformChoices")).toList().size(), 1);
        // A list box for the campaign and a button, as one undo step.
        QVariantMap campaign = lookups.at(1).toMap();
        campaign.insert(str("control"), str("list"));
        lookups[1] = campaign;
        const QVariantList controls{QVariantMap{
            {str("kind"), str("button")}, {str("caption"), str("Totals")},
            {str("actions"), QVariantList{QVariantMap{{str("action"), str("openQuery")},
                                                      {str("arguments"), QVariantMap{{str("query"), str("Totals by Donor")}}}}}}}};
        const QVariantMap saved = f.service.saveFormControls(str("Donation Entry"), controls, lookups);
        QVERIFY2(ok(saved), qPrintable(saved.value(str("error")).toString()));
        QVariantMap again = f.service.formControls(str("Donation Entry"));
        QCOMPARE(again.value(str("lookups")).toList().at(1).toMap().value(str("control")).toString(), str("list"));
        QCOMPARE(names(again.value(str("controls")).toList(), "id"), QStringList{str("button1")});
        const QVariantList nowhere{QVariantMap{{str("kind"), str("subform")}, {str("caption"), str("X")},
                                               {str("table"), str("nowhere")}, {str("linkChild"), str("a")},
                                               {str("linkParent"), str("id")}}};
        QVERIFY(!ok(f.service.saveFormControls(str("Donation Entry"), nowhere, lookups)));
        f.undo()->undo();
        QVERIFY(f.service.formControls(str("Donation Entry")).value(str("controls")).toList().isEmpty());
        // The subform's datasheet: one donor's gifts, and new ones join them.
        DatasheetModel *gifts = f.service.subformSheet(0);
        QVERIFY(gifts->openLinked(str("donations"), str("donor_id"), 1));
        const int ada = f.scalar(str("SELECT count(*) FROM donations WHERE donor_id = 1")).toInt();
        QCOMPARE(gifts->recordCount(), ada);
        QCOMPARE(gifts->grid()->columnIndex(str("donor_id")), -1); // the link is not shown
        QVERIFY(ok(gifts->setCell(str("new"), str("amount"), str("12.50"))));
        QCOMPARE(gifts->recordCount(), ada + 1);
        QCOMPARE(f.scalar(str("SELECT count(*) FROM donations WHERE donor_id = 1")).toInt(), ada + 1);
        QVERIFY(gifts->openLinked(str("donations"), str("donor_id"), QVariant()));
        QCOMPARE(gifts->recordCount(), 0);
        QVERIFY(!gifts->editable());
        QVERIFY(f.service.subformSheet(4) == nullptr);
    }

    void reportsTotalExactlyAndLabelsPlace()
    {
        Fixture f;
        QVERIFY(ok(f.service.openReport(str("Donations by Donor"), {})));
        QindaTK::RichTextDocument pages;
        const QVariantMap report{
            {str("tableName"), str("Donation Details")},
            {str("headerText"), str("Donations")},
            {str("groups"), QVariantList{QVariantMap{{str("field"), str("Donor")},
                                                     {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")},
                                                                                              {str("function"), str("sum")}}}}}}},
            {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")}, {str("function"), str("sum")}}}}};
        const QVariantMap built = f.service.buildReport(&pages, report);
        QVERIFY2(ok(built), qPrintable(built.value(str("error")).toString()));
        QCOMPARE(built.value(str("rowCount")).toInt(), 120);
        QCOMPARE(built.value(str("groupCount")).toInt(), f.scalar(str("SELECT count(DISTINCT donor_id) FROM donations")).toInt());
        const qint64 sum = f.scalar(str("SELECT sum(amount) FROM donations")).toLongLong();
        QVERIFY2(pages.text().contains(str("Report total · Count 120 · Sum of amount ") + money(sum)), qPrintable(money(sum)));
        // Mailing labels: 40 donors on two sheets of 30, by name.
        const QVariantMap labels = f.service.openLabels(str("Donor Mailing Labels"), {});
        QVERIFY2(ok(labels), qPrintable(labels.value(str("error")).toString()));
        QCOMPARE(labels.value(str("pageCount")).toInt(), 2);
        LabelSheet *sheet = f.service.labels();
        QCOMPARE(sheet->pageLabels(0).size(), 30);
        QCOMPARE(sheet->pageLabels(1).size(), 10);
        const QString first = f.scalar(str("SELECT name FROM donors ORDER BY name LIMIT 1")).toString();
        QCOMPARE(sheet->pageLabels(0).first().toMap().value(str("lines")).toStringList().first(), first);
        QCOMPARE(sheet->pageLayout().pageSize().sizePoints(), QSize(612, 792));
        const QVariantMap spec{{str("source"), QVariantMap{{str("kind"), str("table")}, {str("name"), str("donors")}}},
                               {str("product"), str("Avery US Letter|5163")},
                               {str("lines"), QStringList{str("[name]"), str("[city]")}}};
        QCOMPARE(f.service.previewLabels(spec).value(str("labels")).toList().size(), 6);
        QVERIFY(ok(f.service.createLabels(str("Shipping"), spec)));
        QCOMPARE(f.service.reportInfo(str("Shipping")).value(str("type")).toString(), str("labels"));
        QVariantMap period = spec;
        period.insert(str("source"), QVariantMap{{str("kind"), str("query")}, {str("name"), str("Donations in a Period")}});
        period.insert(str("lines"), QStringList{str("[Donor]")});
        QVERIFY(f.service.previewLabels(period).value(str("needsParameters")).toBool());
        QVERIFY(ok(f.service.createLabels(str("Period Labels"), period)));
        QCOMPARE(f.service.reportParameters(str("Period Labels")).size(), 2);
    }

    void xmlExportForAccess()
    {
        Fixture f;
        QVERIFY(ok(f.service.exportObject(str("table"), str("donors"), f.url("donors.xml"), str("xml"))));
        QFile data(f.dir.filePath(str("donors.xml")));
        QFile schema(f.dir.filePath(str("donors.xsd")));
        QVERIFY(data.open(QIODevice::ReadOnly));
        QVERIFY(schema.open(QIODevice::ReadOnly));
        const QByteArray xml = data.readAll();
        QCOMPARE(xml.count("<donors>"), 40);
        QVERIFY(xml.contains("noNamespaceSchemaLocation=\"donors.xsd\""));
        const QByteArray xsd = schema.readAll();
        QVERIFY(xsd.contains("od:jetType=\"autonumber\""));
        QVERIFY(xsd.contains("index-name=\"PrimaryKey\""));
    }
};

BASE_APP_TEST_MAIN(BaseM80Test)

#include "tst_qindabase_m80.moc"
