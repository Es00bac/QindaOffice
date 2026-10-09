// SPDX-License-Identifier: GPL-3.0-or-later
// M80's macros, form controls, reports, labels and XML (D-089 §4–8)
// against hand-worked results: macros and form controls checked against
// the database and kept as objects; where
// conditions compiled safely; reports grouped two levels deep with exact
// money totals; mailing labels placed on Avery sheets with blank lines
// closed up; Access's XML data file and schema.

#include "base_fixture.h"

#include "base_form_controls.h"
#include "base_labels.h"
#include "base_macro.h"
#include "base_objects.h"
#include "base_report.h"
#include "base_templates.h"
#include "base_xml_export.h"

#include <QXmlStreamReader>

using namespace BaseTest;

namespace {

// Six gifts with hand-worked totals: Ada 30.31 (Card 30.30, Check 0.01),
// Alan 33.33, Grace 100.00 (and one gift with no amount); 163.64 in all.
struct Gifts : Db {
    Gifts()
    {
        create(table("gifts", {field("id", FieldType::AutoNumber), field("donor", FieldType::Text),
                               field("method", FieldType::Text), field("amount", FieldType::Currency),
                               field("given", FieldType::Date)}));
        exec(str("INSERT INTO gifts(donor, method, amount, given) VALUES"
                 "('Ada', 'Card', 1010, '2026-01-02'), ('Ada', 'Card', 2020, '2026-01-05'),"
                 "('Ada', 'Check', 1, '2026-02-01'), ('Grace', 'Card', 10000, '2026-01-10'),"
                 "('Grace', 'Cash', NULL, '2026-03-01'), ('Alan', 'Check', 3333, '2026-02-02')"));
    }
};

const ReportTotalValue *findTotal(const ReportRun &run, int level, const QStringList &key, const char *field,
                                  const char *function)
{
    for (const ReportTotalValue &t : run.totals) {
        if (t.level == level && t.key == key && t.field == str(field) && t.function == str(function)) {
            return &t;
        }
    }
    return nullptr;
}

QStringList bandTexts(const ReportRun &run, ReportBand::Kind kind)
{
    QStringList out;
    for (const ReportBand &b : run.bands) {
        if (b.kind == kind) {
            out.append(b.kind == ReportBand::Kind::Detail ? b.cells.join(QLatin1Char('|')) : b.text);
        }
    }
    return out;
}

} // namespace

class BaseMacroReportTest final : public QObject {
    Q_OBJECT
private slots:
    void macrosAreCheckedAndKept()
    {
        Db db;
        QVERIFY(applyTemplate(*db.file, str("donors")).ok);
        Macro m;
        m.steps = {{str("openForm"), {{str("form"), str("Donor")}, {str("where"), str("[city] = \"Portland\"")}}},
                   {str("goToRecord"), {{str("record"), str("new")}}},
                   {str("messageBox"), {{str("message"), str("Ready for a new donor.")}}}};
        QVERIFY(checkMacro(*db.file, m).isEmpty());
        Macro bad = m;
        bad.steps[0].arguments[str("where")] = str("[town] = 1");
        bad.steps[1].arguments[str("record")] = str("sideways");
        bad.steps.append({str("openReport"), {{str("report"), str("Nope")}}});
        bad.steps.append({str("openTable"), {}});
        bad.steps.append({str("runQuerySql"), {}});
        const QStringList problems = checkMacro(*db.file, bad);
        QCOMPARE(problems.size(), 5);
        QVERIFY2(problems.at(0).startsWith(str("Step 1 (OpenForm): the where condition:")), qPrintable(problems.at(0)));
        QCOMPARE(problems.at(1), str("Step 2 (GoToRecord): Record is one of first, previous, next, last, new."));
        QCOMPARE(problems.at(2), str("Step 4 (OpenReport): there is no report named Nope."));
        QCOMPARE(problems.at(3), str("Step 5 (OpenTable): fill in Table name."));
        QCOMPARE(problems.at(4), str("Step 6: there is no macro action \"runQuerySql\"."));
        // Kept as an object, unknown members included; a macro may not run itself.
        QVariantMap saved = m.toMap();
        saved.insert(str("color"), str("teal"));
        QVERIFY(ObjectStore::save(*db.file, str("macro"), str("New Donor"), saved, false, nullptr).ok);
        const std::optional<SavedObject> loaded = ObjectStore::load(*db.file, str("macro"), str("New Donor"));
        QVERIFY(loaded);
        const Macro back = Macro::fromMap(loaded->definition);
        QCOMPARE(back.steps.size(), 3);
        QCOMPARE(back.steps.at(0).arguments.value(str("form")).toString(), str("Donor"));
        QCOMPARE(back.toMap().value(str("color")).toString(), str("teal"));
        Macro loop;
        loop.steps = {{str("runMacro"), {{str("macro"), str("New Donor")}}}};
        QVERIFY(checkMacro(*db.file, loop).isEmpty());
        QCOMPARE(checkMacro(*db.file, loop, str("New Donor")).first(), str("Step 1 (RunMacro): a macro cannot run itself."));
        QCOMPARE(macroActions().size(), 14);
        QVERIFY(macroAction(str("applyFilter"))->arguments.first().required);
    }

    void formControlsAreChecked()
    {
        Db db;
        QVERIFY(applyTemplate(*db.file, str("donors")).ok);
        const QList<FormControl> choices = subformChoices(*db.file, str("donors"));
        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices.first().table, str("donations"));
        QCOMPARE(choices.first().linkChild, str("donor_id"));
        QCOMPARE(choices.first().linkParent, str("id"));
        FormControl subform = choices.first();
        subform.id = newControlId({}, str("subform"));
        QCOMPARE(subform.id, str("subform1"));
        FormControl button;
        button.kind = str("button");
        button.id = newControlId({subform}, str("button"));
        button.caption = str("New donation");
        button.actions = {{str("openForm"), {{str("form"), str("Donation Entry")}, {str("dataMode"), str("add")}}}};
        QVERIFY(checkFormControls(*db.file, str("donors"), {subform, button}).isEmpty());
        FormControl broken = button;
        broken.actions = {{str("openForm"), {{str("form"), str("Nope")}}}};
        FormControl named = button;
        named.id = str("button3");
        named.actions.clear();
        named.macro = str("Missing");
        FormControl wrongLink = subform;
        wrongLink.id = str("subform2");
        wrongLink.linkChild = str("nope");
        const QStringList problems = checkFormControls(*db.file, str("donors"), {subform, button, broken, named, wrongLink});
        QCOMPARE(problems.size(), 4);
        QVERIFY(problems.contains(str("New donation: every control needs its own id.")));
        QVERIFY(problems.contains(str("Button New donation: Step 1 (OpenForm): there is no form named Nope.")));
        QVERIFY(problems.contains(str("Button New donation: there is no macro named Missing.")));
        QVERIFY(problems.contains(str("Subform donations: donations has no field nope.")));
        // Kept in the form's definition, unknown members included.
        QVariantList list = controlsToList({subform, button});
        QVariantMap first = list.first().toMap();
        first.insert(str("height"), 240);
        list[0] = first;
        const QList<FormControl> back = formControls({{str("controls"), list}});
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(1).actions.first().arguments.value(str("dataMode")).toString(), str("add"));
        QCOMPARE(back.at(0).toMap().value(str("height")).toInt(), 240);
    }

    void whereConditionsAreCompiledNotPasted()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name, email) VALUES('Ada', 'a@x'), ('Grace', NULL), ('Alan', 'al@x')")).ok);
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount, given) VALUES(1, 2500, '2026-01-05'), "
                            "(2, 1999, '2026-01-06'), (3, 2001, '2026-01-07')")).ok);
        QString error;
        QCOMPARE(*filteredRowIds(*db.file, str("donors"), str("[name] Like \"A*\""), &error), (QList<qint64>{1, 3}));
        QCOMPARE(*filteredRowIds(*db.file, str("donors"), str("[email] Is Null Or [name] = \"Alan\""), &error),
                 (QList<qint64>{2, 3}));
        // "20" on a currency field is twenty dollars: 2000 minor units.
        QCOMPARE(*filteredRowIds(*db.file, str("donations"), str("[amount] > 20"), &error), (QList<qint64>{1, 3}));
        QCOMPARE(filteredRowIds(*db.file, str("donors"), str(""), &error)->size(), 3);
        QCOMPARE(filteredRowIds(*db.file, str("donors"), str("[name] = \"x' OR 1=1 --\""), &error)->size(), 0);
        QVERIFY(!filteredRowIds(*db.file, str("donors"), str("[town] = 1"), &error));
        QVERIFY2(error.contains(str("town")), qPrintable(error));
        QVERIFY(!compileWhere(*db.file, str("donors"), str("Count([id]) > 1")).ok());
        QVERIFY(!compileWhere(*db.file, str("nowhere"), str("[id] = 1")).ok());
    }

    void reportsTotalEveryGroupExactly()
    {
        Gifts db;
        QString error;
        const std::optional<ReportSource> source = readReportSource(*db.file, str("table"), str("gifts"), {}, &error);
        QVERIFY2(source, qPrintable(error));
        QCOMPARE(source->rows.size(), 6);
        ReportSpec spec;
        spec.title = str("Gifts");
        spec.columns = {{str("given"), str("Date")}, {str("amount"), QString()}};
        ReportGroup donor{str("donor"), QString(), true,
                          {{str("amount"), str("sum"), QString()}, {str("amount"), str("average"), QString()},
                           {str("amount"), str("countValues"), QString()}, {str("given"), str("maximum"), QString()}}};
        ReportGroup method{str("method"), str("Paid by"), false, {{str("amount"), str("sum"), str("Paid")}}};
        spec.groups = {donor, method};
        spec.totals = {{str("amount"), str("sum"), QString()}, {str("amount"), str("minimum"), QString()}};
        spec.sort = {{str("given"), true}};
        const ReportRun run = runReport(*source, spec, db.file->currency());
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(run.columnLabels, (QStringList{str("Date"), str("amount")}));
        QCOMPARE(run.detailRows, 6);
        QCOMPARE(run.groupCount, 5); // Ada: Card, Check · Alan: Check · Grace: Card, Cash
        QCOMPARE(bandTexts(run, ReportBand::Kind::GroupHeader),
                 (QStringList{str("donor: Ada"), str("Paid by: Card"), str("Paid by: Check"), str("donor: Alan"),
                              str("Paid by: Check"), str("donor: Grace"), str("Paid by: Card"), str("Paid by: Cash")}));
        // Within a group, the newest gift first (the sort key, descending).
        QCOMPARE(bandTexts(run, ReportBand::Kind::Detail).mid(0, 2),
                 (QStringList{str("2026-01-05|$20.20"), str("2026-01-02|$10.10")}));
        const QStringList footers = bandTexts(run, ReportBand::Kind::GroupFooter);
        QCOMPARE(footers.at(0), str("Paid by Card total · Count 2 · Paid $30.30"));
        QCOMPARE(footers.at(2), str("donor Ada total · Count 3 · Sum of amount $30.31 · Average of amount $10.10 · "
                                    "Number of amount 3 · Highest given 2026-02-01"));
        QCOMPARE(footers.at(6), str("Paid by Cash total · Count 1 · Paid —")); // only a Null: no total
        QCOMPARE(findTotal(run, 0, {str("Grace")}, "amount", "average")->value.toLongLong(), qint64(10000));
        QCOMPARE(findTotal(run, 0, {str("Ada")}, "amount", "sum")->value.toLongLong(), qint64(3031));
        QCOMPARE(findTotal(run, 1, {str("Ada"), str("Check")}, "amount", "sum")->text, str("$0.01"));
        const QStringList report = bandTexts(run, ReportBand::Kind::ReportFooter);
        QCOMPARE(report.first(), str("Report total · Count 6 · Sum of amount $163.64 · Lowest amount $0.01"));
        QCOMPARE(findTotal(run, -1, {}, "amount", "sum")->value.toLongLong(), qint64(16364));
        // Only the outermost group that changes starts a page.
        QList<bool> breaks;
        for (const ReportBand &b : run.bands) {
            if (b.kind == ReportBand::Kind::GroupHeader) {
                breaks.append(b.pageBreakBefore);
            }
        }
        QCOMPARE(breaks, (QList<bool>{false, false, false, true, false, true, false, false}));
        // A summary, and the designer's equality filter (case-blind).
        spec.detail = false;
        spec.filterField = str("method");
        spec.filterValue = str("card");
        const ReportRun cards = runReport(*source, spec, db.file->currency());
        QVERIFY(bandTexts(cards, ReportBand::Kind::Detail).isEmpty());
        QCOMPARE(cards.detailRows, 3);
        QCOMPARE(findTotal(cards, -1, {}, "amount", "sum")->value.toLongLong(), qint64(13030));
        spec.groups[0].totals.append({str("donor"), str("sum"), QString()});
        QVERIFY(runReport(*source, spec, db.file->currency()).error.contains(str("not a number field")));
    }

    void reportSpecsReadTheToolkitDesign()
    {
        const QVariantMap report{
            {str("headerText"), str("Donations")},
            {str("pageFooterText"), QString()},
            {str("columns"), QVariantList{str("given"), QVariantMap{{str("field"), str("amount")}, {str("label"), str("Gift")}}}},
            {str("groups"), QVariantList{QVariantMap{{str("field"), str("donor")}, {str("pageBreakBefore"), true},
                                                     {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")},
                                                                                              {str("function"), str("sum")}}}}}}},
            {str("query"), QVariantMap{{str("sort"), QVariantList{QVariantMap{{str("field"), str("given")}, {str("ascending"), false}}}},
                                       {str("filter"), QVariantMap{{str("kind"), str("compare")}, {str("op"), str("eq")},
                                                                   {str("field"), str("method")}, {str("parameter"), str("filterValue")}}},
                                       {str("parameters"), QVariantMap{{str("filterValue"), QVariantMap{{str("type"), str("text")},
                                                                                                         {str("value"), str("Card")}}}}}}},
            {str("page"), QVariantMap{{str("paperId"), str("a4")}, {str("orientation"), str("landscape")}}}};
        const ReportSpec spec = ReportSpec::fromReportMap(report);
        QCOMPARE(spec.title, str("Donations"));
        QVERIFY(spec.pageFooter.isEmpty()); // "" turns the band off
        QCOMPARE(spec.columns.size(), 2);
        QCOMPARE(spec.columns.at(1).second, str("Gift"));
        QCOMPARE(spec.groups.first().totals.first().function, str("sum"));
        QVERIFY(spec.groups.first().pageBreakBefore);
        QVERIFY(spec.sort.first().descending);
        QCOMPARE(spec.filterValue, str("Card"));
        QCOMPARE(spec.paperId, str("a4"));
        QVERIFY(spec.landscape);
        QCOMPARE(ReportSpec::fromReportMap({}).pageFooter, str("Page {page} of {pages}"));
        const ReportSpec legacy = ReportSpec::fromReportMap({{str("groupField"), str("donor")}, {str("sumField"), str("amount")}});
        QCOMPARE(legacy.groups.first().totals.first().field, str("amount"));
    }

    void queryReportsAskForTheirParameters()
    {
        Db db;
        QVERIFY(applyTemplate(*db.file, str("donors")).ok);
        QVERIFY(db.exec(str("INSERT INTO donors(name) VALUES('Ada')")).ok);
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount, given) VALUES(1, 2500, '2026-01-05'), "
                            "(1, 700, '2026-04-01')")).ok);
        QString error;
        QVERIFY(!readReportSource(*db.file, str("query"), str("Donations in a Period"), {}, &error));
        QVERIFY2(error.contains(str("Start date")), qPrintable(error));
        const std::optional<ReportSource> source = readReportSource(
            *db.file, str("query"), str("Donations in a Period"),
            {{str("p_start_date"), str("1/1/2026")}, {str("p_end_date"), str("3/31/2026")}}, &error);
        QVERIFY2(source, qPrintable(error));
        QCOMPARE(source->rows.size(), 1);
        const ReportRun run = runReport(*source, ReportSpec(), db.file->currency());
        QVERIFY(bandTexts(run, ReportBand::Kind::ReportInfo).first().contains(str("Start date = 1/1/2026")));
        QVERIFY(bandTexts(run, ReportBand::Kind::Detail).first().contains(str("$25.00")));
    }

    void labelsFillAveryLabelSheets()
    {
        const std::optional<LabelProduct> avery = labelProduct(str("Avery US Letter|5160"));
        QVERIFY(avery);
        QCOMPARE(avery->count(), 30);
        QCOMPARE(avery->page, QSizeF(612, 792));
        const QList<QStringList> five{{str("a")}, {str("b")}, {str("c")}, {str("d")}, {str("e")}};
        QList<LabelPlacement> placed = placeLabels(*avery, five, 2, 1);
        QCOMPARE(placed.size(), 5);
        QCOMPARE(placed.at(0).rect, QRectF(409.5, 36, 189, 72)); // the third label across
        QCOMPARE(placed.at(1).rect, QRectF(13.5, 108, 189, 72)); // the second row
        QList<QStringList> many;
        for (int i = 0; i < 31; ++i) {
            many.append({QString::number(i)});
        }
        placed = placeLabels(*avery, many, 0, 1);
        QCOMPARE(placed.last().page, 1);
        QCOMPARE(placed.last().rect.topLeft(), QPointF(13.5, 36));
        QCOMPARE(placeLabels(*avery, five, 0, 3).size(), 15);
        QVERIFY(!labelProduct(str("Avery A4/A5|L7160"))->page.isEmpty());

        Db db;
        QVERIFY(db.create(table("contacts", {field("id", FieldType::AutoNumber), field("name", FieldType::Text),
                                             field("street", FieldType::Text), field("street2", FieldType::Text),
                                             field("city", FieldType::Text), field("region", FieldType::Text),
                                             field("postal", FieldType::Text)}))
                    .ok);
        QVERIFY(db.exec(str("INSERT INTO contacts(name, street, street2, city, region, postal) VALUES"
                            "('Ada Lovelace', '1 Analytical Way', NULL, 'Portland', 'OR', '97201'),"
                            "('Grace Hopper', '2 Compiler Ct', 'Suite 5', 'Salem', 'OR', '97301')")).ok);
        LabelSpec spec;
        spec.sourceName = str("contacts");
        spec.lines = {str("[name]"), str("[street]"), str("[street2]"), str("=[city] & \", \" & [region] & \" \" & [postal]")};
        spec.sort = {{str("name"), true}};
        LabelRun run = runLabels(*db.file, spec, {});
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(run.records, 2);
        QCOMPARE(run.pageCount, 1);
        QCOMPARE(run.labels.at(0).lines,
                 (QStringList{str("Grace Hopper"), str("2 Compiler Ct"), str("Suite 5"), str("Salem, OR 97301")}));
        QCOMPARE(run.labels.at(1).lines, (QStringList{str("Ada Lovelace"), str("1 Analytical Way"), str("Portland, OR 97201")}));
        const LabelSpec back = LabelSpec::fromMap(spec.toMap());
        QCOMPARE(back.lines, spec.lines);
        QCOMPARE(back.toMap().value(str("type")).toString(), str("labels"));
        spec.lines = {str("[nope]")};
        QVERIFY(runLabels(*db.file, spec, {}).error.startsWith(str("Label line 1:")));
        // A lookup prints the related record's name.
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name) VALUES('Ada Lovelace')")).ok);
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(1, 2500)")).ok);
        TableDef donations = *readTable(*db.file, str("donations"));
        donations.fields[1].lookup = {str("donors"), str("id"), str("name")};
        QVERIFY(db.apply(planAlterTable(*db.file, *readTable(*db.file, str("donations")), donations)).ok);
        LabelSpec gifts;
        gifts.sourceName = str("donations");
        gifts.lines = {str("[donor_id]"), str("[amount]")};
        run = runLabels(*db.file, gifts, {});
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(run.labels.first().lines, (QStringList{str("Ada Lovelace"), str("$25.00")}));
    }

    void xmlExportsWhatAccessImports()
    {
        auto column = [](const char *name, FieldType type, bool required = false) {
            ColumnInfo c;
            c.name = str(name);
            c.type = type;
            c.required = required;
            c.primaryKey = type == FieldType::AutoNumber;
            return c;
        };
        const QList<ColumnInfo> columns{column("id", FieldType::AutoNumber),   column("first gift", FieldType::Date),
                                        column("amount", FieldType::Currency, true), column("active", FieldType::YesNo),
                                        column("notes", FieldType::LongText), column("big", FieldType::Integer),
                                        column("photo", FieldType::File)};
        const QList<QVariantList> rows{
            {qint64(1), str("2026-03-14"), qint64(2500), qint64(1), str("Line <1> & \"2\""), qint64(5000000000), QByteArray("PNG")},
            {qint64(2), QVariant(), qint64(199), qint64(0), QVariant(), qint64(7), QVariant()}};
        TableDef design = table("donor list", {field("id", FieldType::AutoNumber)});
        const AccessXml xml = accessXml(str("donor list"), columns, rows, CurrencyFormat{}, &design, str("donor list.xsd"),
                                        QDateTime(QDate(2026, 10, 9), QTime(12, 0)));
        QXmlStreamReader data(xml.data);
        QVERIFY(data.readNextStartElement());
        QCOMPARE(data.name().toString(), str("dataroot"));
        QCOMPARE(data.attributes().value(str("generated")).toString(), str("2026-10-09T12:00:00"));
        QList<QVariantMap> records;
        while (data.readNextStartElement()) {
            QCOMPARE(data.name().toString(), str("donor_x0020_list"));
            QVariantMap record;
            while (data.readNextStartElement()) {
                const QString name = data.name().toString();
                record.insert(name, data.readElementText());
            }
            records.append(record);
        }
        QVERIFY2(!data.hasError(), qPrintable(data.errorString()));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.at(0).value(str("first_x0020_gift")).toString(), str("2026-03-14T00:00:00"));
        QCOMPARE(records.at(0).value(str("amount")).toString(), str("25.00"));
        QCOMPARE(records.at(0).value(str("active")).toString(), str("1"));
        QCOMPARE(records.at(0).value(str("notes")).toString(), str("Line <1> & \"2\""));
        QCOMPARE(records.at(0).value(str("big")).toString(), str("5000000000"));
        QCOMPARE(records.at(0).value(str("photo")).toString(), str("UE5H"));
        QVERIFY(!records.at(1).contains(str("first_x0020_gift"))); // a Null is left out
        QCOMPARE(records.at(1).value(str("amount")).toString(), str("1.99"));
        const QString schema = QString::fromUtf8(xml.schema);
        QVERIFY(schema.contains(str("od:jetType=\"currency\"")));
        QVERIFY(schema.contains(str("od:jetType=\"autonumber\"")));
        QVERIFY(schema.contains(str("od:autoUnique=\"yes\"")));
        QVERIFY(schema.contains(str("od:jetType=\"double\""))); // big: beyond Access's Long Integer
        QVERIFY(schema.contains(str("index-name=\"PrimaryKey\"")));
        QVERIFY(schema.contains(str("name=\"first_x0020_gift\"")));
        QXmlStreamReader schemaReader(xml.schema);
        while (!schemaReader.atEnd()) {
            schemaReader.readNext();
        }
        QVERIFY2(!schemaReader.hasError(), qPrintable(schemaReader.errorString()));
        QCOMPARE(xmlName(str("2nd try")), str("_x0032_nd_x0020_try"));
    }
};

BASE_TEST_MAIN(BaseMacroReportTest)

#include "tst_base_macro_report.moc"
