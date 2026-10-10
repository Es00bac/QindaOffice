// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092): Project's task custom fields — the catalog with Project's
// own FieldIDs, values of every kind typed and shown, aliases, lookup
// tables that restrict, formulas, the Custom Field filter and sort, the
// .qplan, Microsoft Project XML byte for byte while unchanged, and what an
// M63–M82 .qplan kept verbatim adopted into the model.

#include "custom_fields.h"
#include "mspdi.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "plan_views.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

using namespace PlanTest;

namespace {

const QLocale kC = QLocale::c();

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

QByteArray tight(const QByteArray &xml)
{
    static const QRegularExpression gaps(QStringLiteral(">\\s+<"));
    return QString::fromUtf8(xml).replace(gaps, QStringLiteral("><")).toUtf8();
}

QByteArray keepSample()
{
    QFile f(QStringLiteral(SOURCE_DIR "/data/project_keep_sample.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void typeInto(Project &p, int index, const char *field, const char *text)
{
    const EditResult r = setCustomField(p, index, s(field), s(text), kC);
    QVERIFY2(r.ok, qPrintable(r.error));
}

} // namespace

class PlanCustomFieldsTest final : public QObject {
    Q_OBJECT
private slots:
    void catalogIsProjects()
    {
        QCOMPARE(customFieldCatalog().size(), 120);
        QCOMPARE(customFieldInfo(s("Text1"))->fieldId, 188743731);
        QCOMPARE(customFieldInfo(s("Text30"))->fieldId, 188744016);
        QCOMPARE(customFieldInfo(s("Number6"))->fieldId, 188743982);
        QCOMPARE(customFieldInfo(s("Flag11"))->fieldId, 188743972);
        QCOMPARE(customFieldInfo(s("Cost4"))->fieldId, 188743938);
        QCOMPARE(customFieldInfo(s("Duration4"))->fieldId, 188743955);
        QCOMPARE(customFieldInfo(s("Start6"))->fieldId, 188743962);
        QCOMPARE(customFieldInfo(s("Finish10"))->fieldId, 188743971);
        QCOMPARE(customFieldByFieldId(188743945)->field, s("Date1"));
        QVERIFY(isCustomField(s("Number20")));
        QVERIFY(!isCustomField(s("name")) && !isCustomField(s("Text31")));
        QCOMPARE(customFieldsOfKind(CustomFieldKind::Flag).size(), 20);
    }

    void valuesOfEveryKind()
    {
        Builder b;
        const int t = b.task(s("Pack"), 5 * kDay);
        Project &p = b.run();
        typeInto(p, t, "Text1", "Design");
        typeInto(p, t, "Number1", "12.5");
        typeInto(p, t, "Flag1", "yes");
        typeInto(p, t, "Date1", "2026-03-09");
        typeInto(p, t, "Finish1", "2026-03-09");
        typeInto(p, t, "Cost1", "1,234.50");
        typeInto(p, t, "Duration1", "3d");
        typeInto(p, t, "Duration2", "2ed");
        const Task &task = p.tasks.at(t);
        QCOMPARE(task.custom.value(s("Number1")), s("12.5"));
        QCOMPARE(task.custom.value(s("Flag1")), s("1"));
        QCOMPARE(task.custom.value(s("Date1")), s("2026-03-09T08:00:00"));
        QCOMPARE(task.custom.value(s("Finish1")), s("2026-03-09T17:00:00"));
        QCOMPARE(task.custom.value(s("Cost1")), s("123450"));
        QCOMPARE(task.custom.value(s("Duration1")), s("PT24H0M0S|7"));
        QCOMPARE(task.custom.value(s("Duration2")), s("PT48H0M0S|8"));
        QCOMPARE(customFieldText(p, t, s("Text1"), kC), s("Design"));
        QCOMPARE(customFieldText(p, t, s("Number1"), kC), s("12.5"));
        QCOMPARE(customFieldText(p, t, s("Flag1"), kC), s("Yes"));
        QCOMPARE(customFieldText(p, t, s("Flag2"), kC), s("No"));
        QCOMPARE(customFieldText(p, t, s("Date1"), kC), formatDate(at(9), kC));
        QCOMPARE(customFieldText(p, t, s("Cost1"), kC), s("$1,234.50"));
        QCOMPARE(customFieldText(p, t, s("Duration1"), kC), s("3 days"));
        QCOMPARE(customFieldText(p, t, s("Duration2"), kC), s("2 edays"));
        QCOMPARE(customFieldText(p, t, s("Date2"), kC), s("NA"));
        // Through the sheet's own field API, too.
        QVERIFY(setTaskField(p, t, s("Text2"), s("North"), kC).ok);
        QCOMPARE(taskFieldText(p, t, s("Text2"), kC), s("North"));
        QVERIFY(isTaskFieldEditable(s("Text2")));
        QVERIFY(!setCustomField(p, t, s("Number2"), s("twelve"), kC).ok);
        QVERIFY(!setCustomField(p, t, s("Flag3"), s("maybe"), kC).ok);
        QVERIFY(!setCustomField(p, t, s("Start1"), s("someday"), kC).ok);
        typeInto(p, t, "Text1", "");
        QVERIFY(!p.tasks.at(t).custom.contains(s("Text1")));
        QCOMPARE(*customFieldValue(p, t, s("Cost1")), QVariant(123450.0));
        QCOMPARE(*customFieldValue(p, t, s("Duration1")), QVariant(1440.0));
    }

    void aliasesAndLookups()
    {
        Builder b;
        const int t = b.task(s("Pack"), kDay);
        Project &p = b.run();
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Department")).ok);
        QCOMPARE(customFieldTitle(p, s("Text1")), s("Department"));
        QVERIFY(!setCustomFieldAlias(p, s("Text2"), s("department")).ok); // taken
        QVERIFY(setCustomFieldLookup(p, s("Text1"), {{s("Design"), s("Studio")}, {s("Build"), {}}}, true).ok);
        QVERIFY(!setCustomField(p, t, s("Text1"), s("Paint"), kC).ok);
        typeInto(p, t, "Text1", "Design");
        QVERIFY(!setCustomFieldLookup(p, s("Flag1"), {{s("1"), {}}}, false).ok);
        QVERIFY(!setCustomFieldLookup(p, s("Text3"), {{s("A"), {}}, {s("A"), {}}}, false).ok);
        typeInto(p, t, "Number4", "7");
        QCOMPARE(customFieldsInUse(p), QStringList({s("Text1"), s("Number4")}));
        // An empty alias, table and formula remove the definition.
        QVERIFY(setCustomFieldLookup(p, s("Text1"), {}, false).ok);
        QVERIFY(setCustomFieldAlias(p, s("Text1"), QString()).ok);
        QVERIFY(customFieldDef(p, s("Text1")) == nullptr);
    }

    void formulas()
    {
        Builder b;
        const int t = b.task(s("Pack"), 5 * kDay);
        b.p.tasks[t].fixedCost = 150000; // $1,500.00
        Project &p = b.run();
        typeInto(p, t, "Text1", "Design");
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Department")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Number2"), s("[Duration]/480")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Text2"), s("IIf([% Complete]=100,\"Done\",\"Open\")")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Flag2"), s("[Cost]>1000")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Date2"), s("[Finish]+7")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Number3"), s("ProjDateDiff([Start],[Finish])/60")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Text3"), s("[Department] & \"!\" & Mid(\"xyz\", 2, 1)")).ok);
        QCOMPARE(customFieldText(p, t, s("Number2"), kC), s("5"));
        QCOMPARE(customFieldText(p, t, s("Text2"), kC), s("Open"));
        QCOMPARE(customFieldText(p, t, s("Flag2"), kC), s("Yes"));
        QCOMPARE(customFieldText(p, t, s("Date2"), kC), formatDate(at(13, 17), kC)); // Fri 6 Mar 17:00 + 7 days
        QCOMPARE(customFieldText(p, t, s("Number3"), kC), s("40"));
        QCOMPARE(customFieldText(p, t, s("Text3"), kC), s("Design!y"));
        setPercentComplete(p, {t}, 100);
        schedule(p);
        QCOMPARE(customFieldText(p, t, s("Text2"), kC), s("Done"));
        QVERIFY(!setCustomField(p, t, s("Number2"), s("3"), kC).ok); // a formula field is not typed
        QVERIFY(!formulaProblem(p, s("[Duration]/")).isEmpty());
        QVERIFY(formulaProblem(p, s("[Nope]*2")).contains(s("[Nope]")));
        QVERIFY(!formulaProblem(p, s("Foo(1)")).isEmpty());
        QVERIFY(!setCustomFieldFormula(p, s("Number9"), s("(1+")).ok);
        // Formulas that read each other in a circle show an error.
        QVERIFY(setCustomFieldFormula(p, s("Number4"), s("[Number5]+1")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Number5"), s("[Number4]+1")).ok);
        QCOMPARE(customFieldText(p, t, s("Number4"), kC), s("#ERROR"));
        QVERIFY(evaluateFormula(p, t, s("1/0")).error.contains(s("zero")));
        // Taking the formula away keeps what it showed.
        QVERIFY(setCustomFieldFormula(p, s("Number2"), QString()).ok);
        QCOMPARE(p.tasks.at(t).custom.value(s("Number2")), s("5"));
    }

    void filterAndSort()
    {
        Builder b;
        const int a = b.task(s("A"), kDay);
        const int c = b.task(s("B"), kDay);
        const int none = b.task(s("C"), kDay);
        Project &p = b.run();
        typeInto(p, a, "Text1", "Build");
        typeInto(p, c, "Text1", "Design");
        typeInto(p, a, "Number1", "5");
        typeInto(p, c, "Number1", "12");
        const QList<int> rows{a, c, none};
        const auto run = [&](const char *field, const char *test, const char *value) {
            TaskFilter f;
            f.id = s("custom");
            f.field = s(field);
            f.test = s(test);
            if (value != nullptr) {
                f.value = parseCustomFieldValue(p, f.field, s(value), kC).value_or(QVariant());
            }
            return filterRows(p, rows, f, kNoDate);
        };
        QCOMPARE(run("Text1", "equals", "design"), QList<int>({c}));
        QCOMPARE(run("Text1", "contains", "ui"), QList<int>({a}));
        QCOMPARE(run("Text1", "isSet", nullptr), QList<int>({a, c}));
        QCOMPARE(run("Text1", "notSet", nullptr), QList<int>({none}));
        // As in Project, an unset number is 0: it equals 0, sorts as 0.
        QCOMPARE(run("Number1", "greater", "6"), QList<int>({c}));
        QCOMPARE(run("Number1", "less", "6"), QList<int>({a, none}));
        QCOMPARE(run("Number1", "notEquals", "5"), QList<int>({c, none}));
        QCOMPARE(run("Number1", "equals", "0"), QList<int>({none}));
        QCOMPARE(run("Number1", "isSet", nullptr), QList<int>({a, c}));
        QCOMPARE(sortedOutline(p, s("custom:Number1")), QList<int>({none, a, c}));
        QCOMPARE(sortedOutline(p, s("custom:Number1:desc")), QList<int>({c, a, none}));
        typeInto(p, a, "Number1", "0"); // the default: stored as unset
        QVERIFY(!p.tasks.at(a).custom.contains(s("Number1")));
        typeInto(p, a, "Flag1", "No");
        QVERIFY(!p.tasks.at(a).custom.contains(s("Flag1")));
        QCOMPARE(sortedOutline(p, s("custom:Text1:desc")), QList<int>({c, a, none}));
    }

    void projectXmlByteForByte()
    {
        Project p;
        QVERIFY(readMspdi(keepSample(), &p).ok);
        const int design = p.taskIndex(1);
        const int print = p.taskIndex(2);
        QCOMPARE(p.tasks.at(design).custom.value(s("Text1")), s("Design"));
        QCOMPARE(p.tasks.at(print).custom.value(s("Text1")), s("Production"));
        QCOMPARE(customFieldTitle(p, s("Text1")), s("Department"));
        const QByteArray same = tight(writeMspdi(p));
        QVERIFY(same.contains("<ExtendedAttribute><FieldID>188743731</FieldID><FieldName>Text1</FieldName>"
                              "<Alias>Department</Alias></ExtendedAttribute>"));
        QVERIFY(same.contains("<ExtendedAttribute><FieldID>188743731</FieldID><Value>Design</Value></ExtendedAttribute>"));
        QVERIFY(same.contains("<Value>Whole studio</Value>"));  // the summary task's
        QVERIFY(same.contains("<FieldID>205520904</FieldID>")); // the resource's
        // Changed here: written from the model; the rest replayed.
        typeInto(p, print, "Text1", "Print shop");
        typeInto(p, design, "Number1", "3");
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Team")).ok);
        schedule(p);
        const QByteArray out = writeMspdi(p);
        const QByteArray t = tight(out);
        QVERIFY(t.contains("<ExtendedAttribute><FieldID>188743731</FieldID><Value>Design</Value></ExtendedAttribute>"));
        QVERIFY(t.contains("<ExtendedAttribute><FieldID>188743731</FieldID><Value>Print shop</Value></ExtendedAttribute>"));
        QVERIFY(!t.contains("<Value>Production</Value>"));
        QVERIFY(t.contains("<ExtendedAttribute><FieldID>188743767</FieldID><Value>3</Value></ExtendedAttribute>"));
        QVERIFY(t.contains("<FieldID>188743731</FieldID><FieldName>Text1</FieldName><Alias>Team</Alias>"));
        QVERIFY(t.contains("<ExtendedAttribute><FieldID>188743767</FieldID><FieldName>Number1</FieldName></ExtendedAttribute>"));
        Project back;
        QVERIFY(readMspdi(out, &back).ok);
        QCOMPARE(back.tasks.at(design).custom, p.tasks.at(design).custom);
        QCOMPARE(back.tasks.at(print).custom, p.tasks.at(print).custom);
        QCOMPARE(customFieldTitle(back, s("Text1")), s("Team"));
        QCOMPARE(writeMspdi(back), out);
    }

    void qplanAndAdoption()
    {
        Builder b;
        const int t = b.task(s("Pack"), kDay);
        Project &p = b.run();
        typeInto(p, t, "Cost2", "99");
        QVERIFY(setCustomFieldLookup(p, s("Text4"), {{s("North"), s("Upstairs")}}, true).ok);
        QVERIFY(setCustomFieldFormula(p, s("Number7"), s("[Priority]*2")).ok);
        Project back;
        QVERIFY(readPlanJson(writePlanJson(p), &back).ok);
        QCOMPARE(back.tasks.at(t).custom, p.tasks.at(t).custom);
        QCOMPARE(back.customFields.size(), 2);
        QCOMPARE(customFieldDef(back, s("Text4"))->lookup.first().description, s("Upstairs"));
        QVERIFY(customFieldDef(back, s("Text4"))->restrictToList);
        QCOMPARE(customFieldText(back, t, s("Number7"), kC), s("1000"));
        // An M82 .qplan kept these verbatim; M83 lifts them into the model
        // and writes each once.
        Project old = b.p;
        old.customFields.clear();
        old.tasks[t].custom.clear();
        old.info.extra.insert(s("mspdi"), QJsonArray{QJsonObject{
            {s("name"), s("ExtendedAttributes")},
            {s("xml"), s("<ExtendedAttributes><ExtendedAttribute><FieldID>188743731</FieldID><FieldName>Text1</FieldName>"
                         "<Alias>Area</Alias></ExtendedAttribute></ExtendedAttributes>")}}});
        old.tasks[t].extra.insert(s("mspdi"), QJsonArray{
            QJsonObject{{s("name"), s("Type")}, {s("xml"), s("<Type>2</Type>")}},
            QJsonObject{{s("name"), s("EffortDriven")}, {s("xml"), s("<EffortDriven>1</EffortDriven>")}},
            QJsonObject{{s("name"), s("ExtendedAttribute")},
                        {s("xml"), s("<ExtendedAttribute><FieldID>188743731</FieldID><Value>North</Value></ExtendedAttribute>")}}});
        Project adopted;
        QVERIFY(readPlanJson(writePlanJson(old), &adopted).ok);
        QCOMPARE(int(adopted.tasks.at(t).type), int(TaskType::FixedWork));
        QVERIFY(adopted.tasks.at(t).effortDriven);
        QCOMPARE(adopted.tasks.at(t).custom.value(s("Text1")), s("North"));
        QCOMPARE(customFieldTitle(adopted, s("Text1")), s("Area"));
        const QByteArray xml = tight(writeMspdi(adopted));
        QCOMPARE(xml.count("<Type>2</Type>"), 1);
        QCOMPARE(xml.count("<Value>North</Value>"), 1);
        QCOMPARE(xml.count("<Alias>Area</Alias>"), 1);
    }
};

QTEST_GUILESS_MAIN(PlanCustomFieldsTest)

#include "tst_plan_custom_fields.moc"
