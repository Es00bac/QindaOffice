// SPDX-License-Identifier: GPL-3.0-or-later
// M63 (D-071): Microsoft Project XML keeps what QindaPlan does not model.
// A Project-shaped file with custom fields (and their definitions), task
// type, priority, deadline, hyperlinks, a contact, a baseline 4, timephased
// data, cost rate tables, a recurring exception, work weeks and elements
// no Project version has written yet reads, writes back with all of it in
// Project's element order, survives a .qplan save, and drops only the
// derived values a reschedule makes stale.

#include "mspdi.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QtTest>

using namespace QindaOffice::Plan;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

// The sample file (data/project_keep_sample.xml).
QByteArray kFileBytes()
{
    QFile f(QStringLiteral(SOURCE_DIR "/data/project_keep_sample.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// The child element names of the n-th <Task> (0 = the summary task).
QStringList taskChildren(const QByteArray &xml, int n)
{
    QXmlStreamReader r(xml);
    int task = -1;
    QStringList out;
    while (r.readNextStartElement()) {
        if (r.name() == u"Tasks") {
            while (r.readNextStartElement()) {
                if (++task == n) {
                    while (r.readNextStartElement()) {
                        out << r.name().toString();
                        r.skipCurrentElement();
                    }
                    return out;
                }
                r.skipCurrentElement();
            }
        } else if (r.name() != u"Project") {
            r.skipCurrentElement();
        }
    }
    return out;
}

// The text between <tag> and </tag> after `from`.
QString element(const QByteArray &xml, const QString &tag, qsizetype from = 0)
{
    const QString text = QString::fromUtf8(xml);
    const qsizetype a = text.indexOf(QLatin1Char('<') + tag + QLatin1Char('>'), from);
    const qsizetype b = text.indexOf(QStringLiteral("</") + tag + QLatin1Char('>'), a);
    return a < 0 || b < 0 ? QString() : text.mid(a + tag.size() + 2, b - a - tag.size() - 2);
}

bool before(const QStringList &names, const char *a, const char *b)
{
    const qsizetype i = names.indexOf(str(a));
    const qsizetype j = names.indexOf(str(b));
    return i >= 0 && j >= 0 && i < j;
}

} // namespace

class TestPlanMspdiKeep final : public QObject {
    Q_OBJECT

private slots:
    void priorityAndDeadlineAreModelled()
    {
        Project p;
        const MspdiResult r = readMspdi(kFileBytes(), &p);
        QVERIFY2(r.ok, qPrintable(r.error));
        const Task &design = p.tasks.at(p.taskIndex(1));
        const Task &print = p.tasks.at(p.taskIndex(2));
        QCOMPARE(design.priority, 900);
        QCOMPARE(print.priority, 100);
        QCOMPARE(design.deadline, toMinutes(QDate(2026, 3, 13), 17 * 60));
        QVERIFY(taskFieldText(p, p.taskIndex(2), str("indicators"), QLocale::c()).contains(str("missed-deadline")));
        QVERIFY(!taskFieldText(p, p.taskIndex(1), str("indicators"), QLocale::c()).contains(str("missed-deadline")));
        QVERIFY(setTaskField(p, p.taskIndex(2), str("priority"), str("750"), QLocale::c()).ok);
        QVERIFY(!setTaskField(p, p.taskIndex(2), str("priority"), str("1200"), QLocale::c()).ok);
        QVERIFY(setTaskField(p, p.taskIndex(2), str("deadline"), str("NA"), QLocale::c()).ok);
        QCOMPARE(p.tasks.at(p.taskIndex(2)).deadline, kNoDate);
        // The recurring exception and the work weeks are reported, not lost.
        const QString warnings = r.warnings.join(QLatin1Char('\n'));
        QVERIFY(warnings.contains(str("Monday review")));
        QVERIFY(warnings.contains(str("work weeks")));
        QVERIFY(warnings.contains(str("baseline 4")));
    }

    void everythingElseRoundTripsInProjectsOrder()
    {
        Project p;
        QVERIFY(readMspdi(kFileBytes(), &p).ok);
        const QByteArray out = writeMspdi(p);
        // Project level: kept values, and an unknown element where it was.
        for (const char *kept : {"<Subject>Spring catalogue launch</Subject>", "<Author>Planner</Author>",
                                 "<GUID>A0000000-0000-4000-8000-000000000001</GUID>", "<HonorConstraints>0</HonorConstraints>",
                                 "<CurrencySymbol>€</CurrencySymbol>", "<Alias>Department</Alias>",
                                 "<FutureProjectThing>kept</FutureProjectThing>"}) {
            QVERIFY2(out.contains(kept), kept);
        }
        QVERIFY(out.indexOf("<Author>") < out.indexOf("<FutureProjectThing>"));
        QVERIFY(out.indexOf("<FutureProjectThing>") < out.indexOf("<ScheduleFromStart>"));
        QVERIFY(out.indexOf("<ExtendedAttributes>") < out.indexOf("<Calendars>"));
        // Calendar: GUID, the recurring exception inside Exceptions, work weeks.
        QVERIFY(out.contains("<GUID>C0000000-0000-4000-8000-000000000001</GUID>"));
        QVERIFY(out.contains("<Name>Monday review</Name>"));
        QVERIFY(out.indexOf("<Name>Monday review</Name>") < out.indexOf("</Exceptions>"));
        QVERIFY(out.contains("<Name>Summer</Name>"));
        QVERIFY(out.indexOf("</Exceptions>") < out.indexOf("<WorkWeeks>"));
        // The summary task keeps its custom field.
        QVERIFY(taskChildren(out, 0).contains(str("ExtendedAttribute")));
        QVERIFY(out.contains("<Value>Whole studio</Value>"));
        // Design: every element, modelled or kept, in Project's order.
        const QStringList design = taskChildren(out, 1);
        for (const char *name : {"Type", "CreateDate", "Contact", "FutureTaskThing", "Priority", "EffortDriven",
                                 "FixedCostAccrual", "Deadline", "Hyperlink", "HyperlinkAddress", "ExtendedAttribute",
                                 "TimephasedData", "ActualDuration", "RemainingDuration"}) {
            QVERIFY2(design.contains(str(name)), name);
        }
        QCOMPARE(design.count(str("Baseline")), 2);
        QVERIFY(before(design, "Name", "Active"));
        QVERIFY(before(design, "Manual", "Type"));
        QVERIFY(before(design, "Type", "IsNull"));
        QVERIFY(before(design, "Contact", "FutureTaskThing"));
        QVERIFY(before(design, "FutureTaskThing", "WBS"));
        QVERIFY(before(design, "OutlineLevel", "Priority"));
        QVERIFY(before(design, "Priority", "Start"));
        QVERIFY(before(design, "Work", "EffortDriven"));
        QVERIFY(before(design, "FixedCost", "FixedCostAccrual"));
        QVERIFY(before(design, "ConstraintDate", "Deadline") || before(design, "CalendarUID", "Deadline"));
        QVERIFY(before(design, "Deadline", "Hyperlink"));
        QVERIFY(before(design, "HyperlinkAddress", "Notes"));
        QVERIFY(before(design, "Notes", "ExtendedAttribute"));
        QVERIFY(before(design, "ExtendedAttribute", "Baseline"));
        QVERIFY(before(design, "Baseline", "TimephasedData"));
        QCOMPARE(design.last(), str("TimephasedData"));
        QVERIFY(before(taskChildren(out, 2), "PredecessorLink", "ExtendedAttribute"));
        // Values as the file had them, not QindaPlan's defaults.
        const qsizetype designAt = out.indexOf("<Name>Design</Name>");
        QCOMPARE(element(out, str("Type"), designAt), str("1"));
        QCOMPARE(element(out, str("FixedCostAccrual"), designAt), str("1"));
        QCOMPARE(element(out, str("Priority"), designAt), str("900"));
        QCOMPARE(element(out, str("Deadline"), designAt), str("2026-03-13T17:00:00"));
        QVERIFY(out.contains("<FutureTaskThing kind=\"new\">kept as written</FutureTaskThing>"));
        QVERIFY(out.contains("https://example.test/brief?a=1&amp;b=2"));
        QVERIFY(out.contains("<Number>4</Number>"));
        // Resource and assignment.
        for (const char *kept : {"<Notes>Lead designer</Notes>", "<RateTable>1</RateTable>", "<AccrueAt>3</AccrueAt>",
                                 "<Value>Studio</Value>", "<WorkContour>3</WorkContour>", "<Delay>0</Delay>"}) {
            QVERIFY2(out.contains(kept), kept);
        }
        QCOMPARE(out.count("<TimephasedData>"), 2);
        // And it reads back to the same project, kept parts included.
        Project again;
        QVERIFY(readMspdi(out, &again).ok);
        QCOMPARE(writeMspdi(again), out);
    }

    void survivesAQplanSave()
    {
        Project p;
        QVERIFY(readMspdi(kFileBytes(), &p).ok);
        const QByteArray original = writeMspdi(p);
        Project reopened;
        const LoadResult loaded = readPlanJson(writePlanJson(p), &reopened);
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QCOMPARE(writeMspdi(reopened), original);
    }

    void reschedulingDropsOnlyStaleDerivedValues()
    {
        Project p;
        QVERIFY(readMspdi(kFileBytes(), &p).ok);
        QVERIFY(setTaskField(p, p.taskIndex(1), str("duration"), str("6d"), QLocale::c()).ok);
        schedule(p);
        const QByteArray out = writeMspdi(p);
        const QStringList design = taskChildren(out, 1);
        QVERIFY(!design.contains(str("TimephasedData")));     // its days moved
        QVERIFY(!design.contains(str("RemainingDuration")));  // derived from the old duration
        for (const char *name : {"Type", "Contact", "FutureTaskThing", "Hyperlink", "ExtendedAttribute", "Priority",
                                 "Deadline"}) {
            QVERIFY2(design.contains(str(name)), name);       // the user's own data stays
        }
        QCOMPARE(out.count("<TimephasedData>"), 0);           // the assignment's work moved too
        QVERIFY(out.contains("<WorkContour>3</WorkContour>"));
        QVERIFY(out.contains("<RateTable>1</RateTable>"));    // rates follow the rate, not the work
        // A changed rate takes the stale rate tables out.
        p.resources[p.resourceIndex(1)].standardRate = 7500;
        schedule(p);
        QVERIFY(!writeMspdi(p).contains("<RateTable>1</RateTable>"));
    }
};

QTEST_GUILESS_MAIN(TestPlanMspdiKeep)

#include "tst_plan_mspdi_keep.moc"
