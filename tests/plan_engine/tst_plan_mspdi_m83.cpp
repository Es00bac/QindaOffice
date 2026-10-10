// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092): what QindaPlan now models of Microsoft Project XML reads as
// Project wrote it, schedules as Project did, and goes back byte for byte
// while unchanged — task types and effort driving with the assignments'
// Work and Units, an elapsed duration after an elapsed lag, a split task in
// Project's own daily timephased data, a recurring summary task, and custom
// fields: definitions with an alias, a lookup table and a formula (Project's
// Guid and SecondaryPID kept around them), task values, and a resource's
// field and an unknown FieldID travelling untouched. Project-shaped fixture:
// data/project_m83_sample.xml (its header has the expected schedule).

#include "custom_fields.h"
#include "mspdi.h"
#include "mspdi_keep.h"
#include "mspdi_timephased.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "task_types.h"
#include "tracking.h"
#include "work_calendar.h"

#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

using namespace PlanTest;

namespace {

const QLocale kC = QLocale::c();

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

QByteArray sample()
{
    QFile f(QStringLiteral(SOURCE_DIR "/data/project_m83_sample.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Project readSample(QStringList *warnings = nullptr)
{
    Project p;
    const MspdiResult r = readMspdi(sample(), &p);
    if (warnings != nullptr) {
        *warnings = r.warnings;
    }
    return p;
}

// Whitespace between elements removed, so a replayed element (re-indented
// by the writer) compares with the file's text.
QByteArray tight(const QByteArray &xml)
{
    static const QRegularExpression gaps(QStringLiteral(">\\s+<"));
    return QString::fromUtf8(xml).replace(gaps, QStringLiteral("><")).toUtf8();
}

// The <Task>, <Resource> or <Assignment> element of `uid` in tight `xml`.
QByteArray element(const QByteArray &xml, const char *kind, int uid)
{
    const QByteArray open = QByteArray("<") + kind + "><UID>" + QByteArray::number(uid) + "</UID>";
    const qsizetype from = xml.indexOf(open);
    if (from < 0) {
        return {};
    }
    const QByteArray close = QByteArray("</") + kind + ">";
    return xml.mid(from, xml.indexOf(close, from) + close.size() - from);
}

// Every <name>…</name> in tight `xml`, in order.
QList<QByteArray> fragments(const QByteArray &xml, const QByteArray &name)
{
    QList<QByteArray> out;
    const QByteArray open = "<" + name + ">";
    const QByteArray close = "</" + name + ">";
    for (qsizetype from = xml.indexOf(open); from >= 0; from = xml.indexOf(open, from + 1)) {
        out.append(xml.mid(from, xml.indexOf(close, from) + close.size() - from));
    }
    return out;
}

int indexOf(const Project &p, const char *name)
{
    for (int i = 0; i < p.tasks.size(); ++i) {
        if (p.tasks.at(i).name == QLatin1String(name)) {
            return i;
        }
    }
    qFatal("no task %s", name);
}

const Task &named(const Project &p, const char *name)
{
    return p.tasks.at(indexOf(p, name));
}

// What M83 models, the same in both projects.
void compareM83(const Project &a, const Project &b)
{
    QCOMPARE(int(b.info.defaultTaskType), int(a.info.defaultTaskType));
    QCOMPARE(b.info.newTasksEffortDriven, a.info.newTasksEffortDriven);
    QCOMPARE(b.info.editableActualCosts, a.info.editableActualCosts);
    QCOMPARE(b.customFields.size(), a.customFields.size());
    for (int k = 0; k < a.customFields.size(); ++k) {
        const CustomFieldDef &x = a.customFields.at(k);
        const CustomFieldDef &y = b.customFields.at(k);
        QCOMPARE(y.field, x.field);
        QCOMPARE(y.alias, x.alias);
        QCOMPARE(y.lookup, x.lookup);
        QCOMPARE(y.restrictToList, x.restrictToList);
        QCOMPARE(y.formula, x.formula);
    }
    const QStringList fields = customFieldsInUse(a);
    QCOMPARE(customFieldsInUse(b), fields);
    QCOMPARE(b.tasks.size(), a.tasks.size());
    for (int i = 0; i < a.tasks.size(); ++i) {
        const Task &x = a.tasks.at(i);
        const Task &y = b.tasks.at(i);
        QCOMPARE(y.name, x.name);
        QCOMPARE(int(y.type), int(x.type));
        QCOMPARE(y.effortDriven, x.effortDriven);
        QCOMPARE(y.durationElapsed, x.durationElapsed);
        QCOMPARE(y.duration, x.duration);
        QCOMPARE(y.work, x.work);
        QCOMPARE(y.splits, x.splits);
        QCOMPARE(y.recurring, x.recurring);
        QCOMPARE(y.predecessors, x.predecessors);
        QCOMPARE_AT(y.start, x.start);
        QCOMPARE_AT(y.finish, x.finish);
        for (const QString &f : fields) {
            QCOMPARE(customFieldWireValue(b, i, f), customFieldWireValue(a, i, f));
        }
    }
    QCOMPARE(b.assignments.size(), a.assignments.size());
    for (int k = 0; k < a.assignments.size(); ++k) {
        const Assignment &x = a.assignments.at(k);
        const Assignment &y = b.assignments.at(k);
        QCOMPARE(y.units, x.units);
        QCOMPARE(y.work, x.work);
        QCOMPARE(assignmentSpan(b.tasks.at(b.taskIndex(y.taskUid)), y), assignmentSpan(a.tasks.at(a.taskIndex(x.taskUid)), x));
    }
}

QString period(int type, const char *from, const char *to, const char *value)
{
    return s("<TimephasedData><Type>%1</Type><UID>1</UID><Start>%2</Start><Finish>%3</Finish><Unit>2</Unit>"
             "<Value>%4</Value></TimephasedData>")
        .arg(type)
        .arg(s(from), s(to), s(value));
}

} // namespace

class PlanMspdiM83Test final : public QObject {
    Q_OBJECT
private slots:
    void readsWhatProjectWrote()
    {
        QStringList warnings;
        const Project p = readSample(&warnings);
        QVERIFY2(!p.tasks.isEmpty(), "the fixture did not read");
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        QCOMPARE(int(p.info.defaultTaskType), int(TaskType::FixedWork));
        QVERIFY(p.info.newTasksEffortDriven);
        QVERIFY(p.info.editableActualCosts);
        // Fixed Work, effort driven: two people share 32 hours over 2 days.
        const Task &wire = named(p, "Wire the floor");
        QCOMPARE(int(wire.type), int(TaskType::FixedWork));
        QVERIFY(wire.effortDriven);
        QCOMPARE(wire.duration, 2 * kDay);
        QCOMPARE(wire.work, 32 * kHour);
        QCOMPARE(p.assignmentsOfTask(wire.uid).size(), 2);
        for (int ai : p.assignmentsOfTask(wire.uid)) {
            const Assignment &a = p.assignments.at(ai);
            QCOMPARE(a.units, 100);
            QCOMPARE(a.workInput, kWorkFollowsDuration); // 16 h = 2 days at 100 %
            QCOMPARE(assignmentWork(wire, a), 16 * kHour);
        }
        QCOMPARE_AT(wire.finish, at(3, 17));
        // 2 elapsed days after an elapsed day's lag: Wednesday 17:00 to Friday 17:00.
        const Task &cure = named(p, "Cure the screed");
        QVERIFY(cure.durationElapsed);
        QCOMPARE(cure.duration, 2 * 1440);
        QCOMPARE(cure.predecessors.size(), 1);
        QVERIFY(cure.predecessors.first().lagElapsed);
        QCOMPARE(cure.predecessors.first().lag, 1440);
        QCOMPARE_AT(cure.start, at(4, 17));
        QCOMPARE_AT(cure.finish, at(6, 17));
        // Fixed Duration, split on Tuesday by Project's daily periods.
        const Task &fit = named(p, "Fit cabinets");
        QCOMPARE(int(fit.type), int(TaskType::FixedDuration));
        QVERIFY(fit.effortDriven);
        QCOMPARE(fit.splits, (QList<TaskSplit>{{kDay, kDay}}));
        QCOMPARE(fit.duration, 3 * kDay);
        QCOMPARE_AT(fit.start, at(9));
        QCOMPARE_AT(fit.finish, at(12, 17));
        QCOMPARE(fit.work, 12 * kHour);
        const Assignment &ben = p.assignments.at(p.assignmentIndex(fit.uid, 2));
        QCOMPARE(ben.units, 50);
        QCOMPARE(ben.workInput, kWorkFollowsDuration);
        // The recurring summary and its occurrences (Start No Earlier Than).
        const Task &snag = named(p, "Snag walk");
        QVERIFY(snag.recurring && snag.summary);
        QVERIFY(!snag.recurrence); // Project's XML has no pattern
        QVERIFY(taskFieldText(p, indexOf(p, "Snag walk"), s("indicators"), kC).contains(s("recurring")));
        QCOMPARE_AT(named(p, "Snag walk 1").start, at(13, 9));
        QCOMPARE_AT(named(p, "Snag walk 2").finish, at(20, 10));
        QCOMPARE_AT(snag.finish, at(20, 10));
        QCOMPARE_AT(named(p, "Owner sign-off").start, at(12, 17));
        QCOMPARE_AT(p.finish, at(20, 10));
        // Custom fields: Text1 renamed with its lookup table, Number1 with its
        // formula; Flag1's bare definition and the resource's only travel.
        QCOMPARE(p.customFields.size(), 2);
        const CustomFieldDef *trade = customFieldDef(p, s("Text1"));
        QVERIFY(trade != nullptr);
        QCOMPARE(trade->alias, s("Trade"));
        QVERIFY(trade->restrictToList);
        QCOMPARE(trade->lookup, (QList<CustomLookupEntry>{{s("Electrical"), s("Sparks")}, {s("Joinery"), QString()}}));
        QCOMPARE(customFieldDef(p, s("Number1"))->formula, s("[Duration]/480"));
        QCOMPARE(customFieldTitle(p, s("Number1")), s("Days"));
        const int w = indexOf(p, "Wire the floor");
        QCOMPARE(customFieldText(p, w, s("Text1"), kC), s("Electrical"));
        QCOMPARE(customFieldText(p, w, s("Flag1"), kC), s("Yes"));
        // The formula, worked here, gives the file's values (minutes ÷ 480).
        QCOMPARE(customFieldWireValue(p, w, s("Number1")), s("2"));
        QCOMPARE(customFieldWireValue(p, indexOf(p, "Cure the screed"), s("Number1")), s("6"));
        QCOMPARE(customFieldWireValue(p, indexOf(p, "Fit cabinets"), s("Number1")), s("3"));
        // The schedule is Project's: each task's Start, Finish and Duration
        // are written as the file has them.
        const QByteArray in = tight(sample());
        const QByteArray out = tight(writeMspdi(p));
        for (int uid = 1; uid <= 7; ++uid) {
            for (const char *tag : {"Start", "Finish", "Duration"}) {
                const QByteArray theirs = fragments(element(in, "Task", uid), tag).value(0);
                QVERIFY(!theirs.isEmpty());
                QVERIFY2(element(out, "Task", uid).contains(theirs), theirs.constData());
            }
        }
    }

    void unchangedGoesBackByteForByte()
    {
        const QByteArray in = tight(sample());
        const Project p = readSample();
        const QByteArray raw = writeMspdi(p);
        const QByteArray out = tight(raw);
        // Every definition — the resource's Text1 among them, with Project's
        // Guid, SecondaryPID and SecondaryGuid — as the file has it.
        QCOMPARE(fragments(out, "ExtendedAttributes").value(0), fragments(in, "ExtendedAttributes").value(0));
        // Each task's values, the unknown FieldID's too, and the resource's.
        int values = 0;
        for (int uid = 1; uid <= 7; ++uid) {
            for (const QByteArray &v : fragments(element(in, "Task", uid), "ExtendedAttribute")) {
                QVERIFY2(element(out, "Task", uid).contains(v), v.constData());
                ++values;
            }
        }
        QCOMPARE(values, 7);
        QVERIFY(element(out, "Task", 7).contains("<ExtendedAttribute><FieldID>188744339</FieldID><Value>1</Value>"
                                                 "</ExtendedAttribute>"));
        QVERIFY(element(out, "Resource", 1).contains("<ExtendedAttribute><FieldID>205520904</FieldID><Value>North</Value>"
                                                     "</ExtendedAttribute>"));
        // Project's own daily periods of the split task.
        const QList<QByteArray> periods = fragments(element(in, "Assignment", 3), "TimephasedData");
        QCOMPARE(periods.size(), 4);
        QCOMPARE(fragments(element(out, "Assignment", 3), "TimephasedData"), periods);
        // What M83 models, in Project's spelling.
        QVERIFY(out.contains("<DefaultTaskType>2</DefaultTaskType>"));
        QVERIFY(out.contains("<EditableActualCosts>1</EditableActualCosts>"));
        QVERIFY(out.contains("<NewTasksEffortDriven>1</NewTasksEffortDriven>"));
        QVERIFY(element(out, "Task", 1).contains("<Type>2</Type>"));
        QVERIFY(element(out, "Task", 1).contains("<Work>PT32H0M0S</Work><EffortDriven>1</EffortDriven><Recurring>0</Recurring>"));
        QVERIFY(element(out, "Task", 2).contains("<Duration>PT48H0M0S</Duration><DurationFormat>8</DurationFormat>"));
        QVERIFY(element(out, "Task", 2).contains("<LinkLag>14400</LinkLag><LagFormat>8</LagFormat>"));
        QVERIFY(element(out, "Task", 3).contains("<Type>1</Type>"));
        QVERIFY(element(out, "Task", 4).contains("<Recurring>1</Recurring>"));
        QVERIFY(element(out, "Assignment", 1).contains("<Units>1.00</Units><Work>PT16H0M0S</Work>"));
        QVERIFY(element(out, "Assignment", 3).contains("<Units>0.50</Units><Work>PT12H0M0S</Work>"));
        // Read back: the same model, and the same file again.
        Project back;
        const MspdiResult r = readMspdi(raw, &back);
        QVERIFY(r.ok);
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
        compareM83(p, back);
        if (QTest::currentTestFailed()) {
            return;
        }
        QCOMPARE(writeMspdi(back), raw);
    }

    void changesAreWrittenFromTheModel()
    {
        Project p = readSample();
        const int wire = indexOf(p, "Wire the floor");
        const int cure = indexOf(p, "Cure the screed");
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Craft")).ok);
        QVERIFY(setCustomFieldFormula(p, s("Number1"), s("[Duration]/60")).ok);
        QVERIFY(setCustomField(p, wire, s("Text1"), s("Joinery"), kC).ok);
        QVERIFY(!setCustomField(p, wire, s("Text1"), s("Plumbing"), kC).ok); // the list restricts
        QVERIFY(setCustomField(p, wire, s("Flag1"), QString(), kC).ok);      // cleared
        QVERIFY(setCustomField(p, cure, s("Text2"), s("Wet trades"), kC).ok);
        schedule(p);
        QByteArray raw = writeMspdi(p);
        QByteArray out = tight(raw);
        // Text1's and Number1's definitions from the model, Project's Guid,
        // SecondaryPID and SecondaryGuid kept in the schema's order.
        QVERIFY(out.contains("<ExtendedAttribute><FieldID>188743731</FieldID><FieldName>Text1</FieldName>"
                             "<Guid>000039B7-8BBE-4CEB-82C4-FA8C0B400033</Guid><Alias>Craft</Alias>"
                             "<SecondaryPID>255869028</SecondaryPID><SecondaryGuid>000039B7-8BBE-4CEB-82C4-FA8C0F404064"
                             "</SecondaryGuid><RestrictValues>1</RestrictValues><ValueList><Value><ID>1</ID><Value>"
                             "Electrical</Value><Description>Sparks</Description></Value><Value><ID>2</ID><Value>Joinery"
                             "</Value></Value></ValueList></ExtendedAttribute>"));
        QVERIFY(out.contains("<FieldName>Number1</FieldName><Guid>000039B7-8BBE-4CEB-82C4-FA8C0B400057</Guid><Alias>Days"
                             "</Alias><SecondaryPID>255869047</SecondaryPID><Formula>[Duration]/60</Formula>"
                             "</ExtendedAttribute>"));
        // Flag1's and the resource's as they were; Text2's added, bare.
        QVERIFY(out.contains("<ExtendedAttribute><FieldID>188743752</FieldID><FieldName>Flag1</FieldName><Guid>"
                             "000039B7-8BBE-4CEB-82C4-FA8C0B400048</Guid></ExtendedAttribute>"));
        QVERIFY(out.contains("<ExtendedAttribute><FieldID>205520904</FieldID><FieldName>Text1</FieldName><Alias>Crew"
                             "</Alias></ExtendedAttribute><ExtendedAttribute><FieldID>188743734</FieldID><FieldName>Text2"
                             "</FieldName></ExtendedAttribute></ExtendedAttributes>"));
        // The values: changed ones from the model, the cleared one gone, the
        // new one after the kept ones, the unknown one still there.
        QVERIFY(element(out, "Task", 1).contains("<ExtendedAttribute><FieldID>188743731</FieldID><Value>Joinery</Value>"
                                                 "</ExtendedAttribute><ExtendedAttribute><FieldID>188743767</FieldID>"
                                                 "<Value>16</Value></ExtendedAttribute></Task>"));
        QVERIFY(element(out, "Task", 2).contains("<ExtendedAttribute><FieldID>188743767</FieldID><Value>48</Value>"
                                                 "</ExtendedAttribute><ExtendedAttribute><FieldID>188743734</FieldID>"
                                                 "<Value>Wet trades</Value></ExtendedAttribute>"));
        QVERIFY(element(out, "Task", 7).contains("<FieldID>188744339</FieldID><Value>1</Value>"));
        Project back;
        QVERIFY(readMspdi(raw, &back).ok);
        compareM83(p, back);
        if (QTest::currentTestFailed()) {
            return;
        }
        QCOMPARE(back.customFields.first().alias, s("Craft"));
        QCOMPARE(writeMspdi(back), raw);

        // Task type and effort driving; one assignment's work typed: on a
        // fixed-work task Ana's 8 h end Monday night, Ben's 16 h Tuesday.
        QVERIFY(setTaskType(p, cure, TaskType::FixedDuration).ok);
        QVERIFY(setEffortDriven(p, cure, true).ok);
        const int ana = p.assignmentIndex(p.tasks.at(wire).uid, 1);
        QVERIFY(setAssignmentWork(p, ana, 8 * kHour).ok);
        schedule(p);
        QCOMPARE(p.tasks.at(wire).work, 24 * kHour);
        QCOMPARE(p.tasks.at(wire).duration, 2 * kDay);
        raw = writeMspdi(p);
        out = tight(raw);
        QVERIFY(element(out, "Task", 2).contains("<Type>1</Type>"));
        QVERIFY(element(out, "Task", 2).contains("<EffortDriven>1</EffortDriven>"));
        QVERIFY(element(out, "Task", 1).contains("<Work>PT24H0M0S</Work>"));
        QVERIFY(element(out, "Assignment", 1).contains("<Finish>2026-03-02T17:00:00</Finish><Start>2026-03-02T08:00:00"
                                                       "</Start><Units>1.00</Units><Work>PT8H0M0S</Work>"));
        QVERIFY(element(out, "Assignment", 2).contains("<Finish>2026-03-03T17:00:00</Finish>"));
        Project again;
        QVERIFY(readMspdi(raw, &again).ok);
        compareM83(p, again);
        if (QTest::currentTestFailed()) {
            return;
        }
        QCOMPARE(again.assignments.at(again.assignmentIndex(p.tasks.at(wire).uid, 1)).workSpan, kDay);
        QCOMPARE(writeMspdi(again), raw);
    }

    void splitsMoveTheirTimephasedData()
    {
        Project p = readSample();
        const int fit = indexOf(p, "Fit cabinets");
        // The work stops on Wednesday instead: the same dates, so the
        // assignment's stamp holds, but Project's periods no longer say it.
        QCOMPARE(removeSplits(p, {fit}), 1);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(fit).finish, at(11, 17));
        QVERIFY(!tight(writeMspdi(p)).contains("<TimephasedData>"));
        QVERIFY(splitTask(p, fit, at(10, 17), at(12, 8)).ok);
        schedule(p);
        QCOMPARE(p.tasks.at(fit).splits, (QList<TaskSplit>{{2 * kDay, kDay}}));
        QCOMPARE_AT(p.tasks.at(fit).finish, at(12, 17));
        const QByteArray raw = writeMspdi(p);
        const QByteArray mine = element(tight(raw), "Assignment", 3);
        const QList<QByteArray> periods = fragments(mine, "TimephasedData");
        QCOMPARE(periods.size(), 3);
        QCOMPARE(periods.at(0), QByteArray("<TimephasedData><Type>1</Type><UID>3</UID><Start>2026-03-09T08:00:00</Start>"
                                           "<Finish>2026-03-10T17:00:00</Finish><Unit>2</Unit><Value>PT8H0M0S</Value>"
                                           "</TimephasedData>"));
        QVERIFY(periods.at(1).contains("<Start>2026-03-10T17:00:00</Start><Finish>2026-03-12T08:00:00</Finish><Unit>2</Unit>"
                                       "<Value>PT0H0M0S</Value>"));
        QVERIFY(periods.at(2).contains("<Start>2026-03-12T08:00:00</Start><Finish>2026-03-12T17:00:00</Finish><Unit>2</Unit>"
                                       "<Value>PT4H0M0S</Value>"));
        Project back;
        QVERIFY(readMspdi(raw, &back).ok);
        QCOMPARE(back.tasks.at(fit).splits, p.tasks.at(fit).splits);
        QCOMPARE(writeMspdi(back), raw);
    }

    void stopAndResumeAreSplits()
    {
        // Project's Reschedule Uncompleted Work: actual work (Type 2) on
        // Monday, the remaining work (Type 1) from Thursday — Tuesday and
        // Wednesday are the split. Daily periods over a weekend are not.
        Builder b;
        const int t = b.task(s("Survey"), 4 * kDay);
        Project &p = b.run();
        const WorkCalendar cal(p, p.info.calendarUid);
        using QindaOffice::Plan::Mspdi::splitsFromTimephased;
        const QStringList resumed{period(2, "2026-03-02T08:00:00", "2026-03-02T17:00:00", "PT8H0M0S"),
                                  period(1, "2026-03-05T08:00:00", "2026-03-09T17:00:00", "PT24H0M0S")};
        QCOMPARE(splitsFromTimephased(resumed, p.tasks.at(t), cal), (QList<TaskSplit>{{kDay, 2 * kDay}}));
        const QStringList weekend{period(1, "2026-03-06T08:00:00", "2026-03-07T08:00:00", "PT8H0M0S"),
                                  period(1, "2026-03-07T08:00:00", "2026-03-09T08:00:00", "PT0H0M0S"),
                                  period(1, "2026-03-09T08:00:00", "2026-03-11T17:00:00", "PT24H0M0S")};
        QVERIFY(splitsFromTimephased(weekend, p.tasks.at(t), cal).isEmpty());
        // Rescheduled here (Monday done, the rest after Wednesday), written
        // as that split and read back.
        setPercentComplete(p, {t}, 25);
        schedule(p);
        QCOMPARE(rescheduleUncompletedWork(p, at(4, 17)), 1);
        schedule(p);
        QCOMPARE(p.tasks.at(t).splits, (QList<TaskSplit>{{kDay, 2 * kDay}}));
        QCOMPARE_AT(p.tasks.at(t).finish, at(9, 17));
        const QByteArray xml = writeMspdi(p);
        QVERIFY(xml.contains("<ResourceUID>-65535</ResourceUID>"));
        Project back;
        QVERIFY(readMspdi(xml, &back).ok);
        QCOMPARE(back.tasks.at(t).splits, p.tasks.at(t).splits);
        QCOMPARE_AT(back.tasks.at(t).finish, at(9, 17));
        QCOMPARE(writeMspdi(back), xml);
    }

    void qplanKeepsItAll()
    {
        const Project p = readSample();
        Project back;
        const LoadResult r = readPlanJson(writePlanJson(p), &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        compareM83(p, back);
        if (QTest::currentTestFailed()) {
            return;
        }
        QCOMPARE(writeMspdi(back), writeMspdi(p));
    }

    void writtenShapeFollowsTheSchema()
    {
        Project p = readSample();
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Craft")).ok);
        QVERIFY(setCustomField(p, indexOf(p, "Cure the screed"), s("Duration3"), s("3ed"), kC).ok);
        schedule(p);
        const QByteArray out = writeMspdi(p);
        QVERIFY(tight(out).contains("<ExtendedAttribute><FieldID>188743785</FieldID><Value>PT72H0M0S</Value>"
                                    "<DurationFormat>8</DurationFormat></ExtendedAttribute>"));
        using QindaOffice::Plan::Mspdi::Kind;
        using QindaOffice::Plan::Mspdi::schemaRank;
        // The schema's ExtendedAttribute definition sequence (mspdi_pj12.xsd).
        const QStringList definition{s("FieldID"), s("FieldName"), s("CFType"), s("Guid"), s("ElemType"),
                                     s("MaxMultiValues"), s("UserDef"), s("Alias"), s("SecondaryPID"),
                                     s("AutoRollDown"), s("DefaultGuid"), s("Ltuid"), s("SecondaryGuid"),
                                     s("PhoneticAlias"), s("RollupType"), s("CalculationType"), s("Formula"),
                                     s("RestrictValues"), s("ValuelistSortOrder"), s("AppendNewValues"), s("Default"),
                                     s("ValueList")};
        const QStringList value{s("FieldID"), s("Value"), s("DurationFormat")};
        static const QRegularExpression integer(QStringLiteral("^-?\\d+$"));
        QXmlStreamReader xml(out);
        QStringList path;
        QList<QStringList> children(1);
        int checked = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                children.last().append(xml.name().toString());
                path.append(xml.name().toString());
                children.append(QStringList());
            } else if (xml.isCharacters() && !xml.isWhitespace() && path.size() >= 2) {
                const QString name = path.last();
                if (name == QLatin1String("Type") || name == QLatin1String("EffortDriven")
                    || name == QLatin1String("Recurring") || name == QLatin1String("LagFormat")
                    || name == QLatin1String("FieldID") || name == QLatin1String("DefaultTaskType")) {
                    const QString text = xml.text().toString();
                    QVERIFY2(integer.match(text).hasMatch(), qPrintable(name + QLatin1Char('=') + text));
                }
            } else if (xml.isEndElement()) {
                const QString name = path.takeLast();
                const QStringList kids = children.takeLast();
                const QString parent = path.isEmpty() ? QString() : path.last();
                QStringList order;
                Kind kind = Kind::Task;
                if (name == QLatin1String("ExtendedAttribute")) {
                    order = parent == QLatin1String("ExtendedAttributes") ? definition : value;
                } else if (name == QLatin1String("Task") || name == QLatin1String("Assignment")
                           || name == QLatin1String("Resource")) {
                    kind = name == QLatin1String("Task") ? Kind::Task
                         : name == QLatin1String("Resource") ? Kind::Resource : Kind::Assignment;
                } else {
                    continue;
                }
                int last = -1;
                for (const QString &kid : kids) {
                    const int rank = order.isEmpty() ? schemaRank(kind, kid) : int(order.indexOf(kid));
                    QVERIFY2(rank >= 0, qPrintable(name + QStringLiteral(": unknown child ") + kid));
                    QVERIFY2(rank >= last, qPrintable(name + QStringLiteral(": ") + kid + QStringLiteral(" out of order")));
                    last = rank;
                }
                ++checked;
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(checked > 25);
    }
};

QTEST_GUILESS_MAIN(PlanMspdiM83Test)

#include "tst_plan_mspdi_m83.moc"
