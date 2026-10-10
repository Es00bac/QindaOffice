// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092) in plan_output: a task sheet exported to Excel comes back
// through the task import (readTaskTableXlsx → guessImportMapping →
// importTasks) with its outline, durations, links and custom fields under
// their new names; and the printed Gantt draws a split task's gap and
// Project's progress line. QQO_GRAB_DIR saves the pages for review.

#include "custom_fields.h"
#include "gantt_print.h"
#include "plan_edit.h"
#include "plan_import.h"
#include "plan_import_xlsx.h"
#include "plan_time.h"
#include "plan_xlsx.h"
#include "scheduler.h"
#include "tracking.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Plan;

namespace {

constexpr Minutes kDay = 480;
const QSizeF kLetterLandscape(792, 612);
const QLocale kC = QLocale::c();

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

Minutes at(int day, int hour = 8)
{
    return toMinutes(QDate(2026, 3, day), hour * 60);
}

int add(Project &p, const char *name, Minutes duration, int level, const QList<int> &preds = {}, Minutes lag = 0)
{
    Task t;
    t.uid = p.nextTaskUid();
    t.guid = QUuid::createUuid();
    t.name = s(name);
    t.duration = duration;
    t.outlineLevel = level;
    for (int pr : preds) {
        t.predecessors.append(Link{p.tasks.at(pr).uid, LinkType::FinishToStart, lag, std::nullopt, false, {}});
    }
    p.tasks.append(t);
    return int(p.tasks.size()) - 1;
}

void grab(const QImage &image, const QString &name)
{
    const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
    if (!dir.isEmpty()) {
        QVERIFY(image.save(dir + QLatin1Char('/') + name + QStringLiteral(".png")));
    }
}

int pixels(const QImage &image, const QColor &c, int tolerance)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor p = image.pixelColor(x, y);
            n += std::abs(p.red() - c.red()) + std::abs(p.green() - c.green()) + std::abs(p.blue() - c.blue()) <= tolerance;
        }
    }
    return n;
}

} // namespace

class PlanOutputM83Test final : public QObject {
    Q_OBJECT
private slots:
    void excelTaskImport()
    {
        Project p = newProject(QDate(2026, 3, 2), s("Fit-out"));
        add(p, "Fit-out", 0, 1);
        const int wire = add(p, "Wire the floor", 2 * kDay, 2);
        const int fit = add(p, "Fit cabinets", 3 * kDay, 2, {wire});
        const int handover = add(p, "Handover", 0, 1, {fit}, kDay);
        schedule(p);
        QVERIFY(setCustomFieldAlias(p, s("Text1"), s("Trade")).ok);
        QVERIFY(setCustomField(p, wire, s("Text1"), s("Electrical"), kC).ok);
        QVERIFY(setCustomField(p, fit, s("Text1"), s("Joinery"), kC).ok);
        QVERIFY(setCustomField(p, fit, s("Number1"), s("4.5"), kC).ok);
        const QStringList fields{s("id"), s("wbs"), s("name"), s("duration"), s("predecessors"), s("Text1"), s("Number1")};
        QTemporaryDir dir;
        const QString path = dir.filePath(s("tasks.xlsx"));
        QString error;
        QVERIFY2(writeTaskSheetXlsx(p, fields, path, kC, &error), qPrintable(error));
        ImportTable table;
        QVERIFY2(readTaskTableXlsx(path, &table, &error), qPrintable(error));
        QCOMPARE(table.rows.size(), 5);
        QCOMPARE(table.rows.first(), QStringList({s("ID"), s("WBS"), s("Task Name"), s("Duration"), s("Predecessors"),
                                                  s("Trade"), s("Number1")}));
        // Into a project that renamed Text1 the same way: every column maps.
        Project into = newProject(QDate(2026, 3, 2), s("Imported"));
        QVERIFY(setCustomFieldAlias(into, s("Text1"), s("Trade")).ok);
        const QStringList mapping = guessImportMapping(table.rows.first(), &into);
        QCOMPARE(mapping, QStringList({s("id"), s("wbs"), s("name"), s("duration"), s("predecessors"), s("Text1"),
                                       s("Number1")}));
        // Without the new name, "Trade" is left for the user to map.
        QCOMPARE(guessImportMapping(table.rows.first()).at(5), QString());
        ImportReport report;
        const EditResult r = importTasks(into, table, mapping, true, &report, kC);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(report.warnings.isEmpty(), qPrintable(report.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(report.tasks, 4);
        schedule(into);
        QCOMPARE(into.tasks.size(), p.tasks.size());
        for (int i = 0; i < p.tasks.size(); ++i) {
            const Task &a = p.tasks.at(i);
            const Task &b = into.tasks.at(i);
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.outlineLevel, a.outlineLevel);
            QCOMPARE(b.summary, a.summary);
            QCOMPARE(b.duration, a.duration);
            QCOMPARE(taskFieldText(into, i, s("predecessors"), kC), taskFieldText(p, i, s("predecessors"), kC));
            QCOMPARE(customFieldWireValue(into, i, s("Text1")), customFieldWireValue(p, i, s("Text1")));
            QCOMPARE(customFieldWireValue(into, i, s("Number1")), customFieldWireValue(p, i, s("Number1")));
            QCOMPARE(toIso(b.start), toIso(a.start));
            QCOMPARE(toIso(b.finish), toIso(a.finish));
        }
        QCOMPARE(toIso(into.tasks.at(handover).finish), toIso(at(9, 17))); // Friday 17:00 + 1 day
        // An unreadable workbook is refused with a reason.
        QFile junk(dir.filePath(s("junk.xlsx")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a workbook");
        junk.close();
        error.clear();
        QVERIFY(!readTaskTableXlsx(junk.fileName(), &table, &error));
        QVERIFY(!error.isEmpty());
    }

    void splitsAndProgressLineOnPaper()
    {
        // Paint 4 days split after Tuesday (Wednesday off); Survey 4 days,
        // a quarter done by Wednesday night (behind); Order should have
        // started Tuesday and has not.
        Project p = newProject(QDate(2026, 3, 2), s("Fit-out"));
        const int paint = add(p, "Paint", 4 * kDay, 1);
        const int survey = add(p, "Survey", 4 * kDay, 1);
        const int order = add(p, "Order", kDay, 1);
        p.tasks[order].constraint = ConstraintType::StartNoEarlierThan;
        p.tasks[order].constraintDate = at(3);
        schedule(p);
        setPercentComplete(p, {survey}, 25);
        p.info.statusDate = at(4, 17);
        schedule(p);
        GanttPrintOptions o;
        o.locale = kC;
        o.statusDate = p.info.statusDate;
        const QImage whole = rasterPages(ganttLayout(p, o), kLetterLandscape, 144).first();
        QVERIFY(splitTask(p, paint, at(3, 17), at(5, 8)).ok);
        schedule(p);
        QCOMPARE(toIso(p.tasks.at(paint).finish), toIso(at(6, 17)));
        const QImage split = rasterPages(ganttLayout(p, o), kLetterLandscape, 144).first();
        grab(split, s("gantt_m83_split"));
        QVERIFY(split != whole);
        // The progress line: bent to Survey's progress (Monday night) and
        // to Order's start; drawn only when asked for.
        const QList<ProgressPoint> line = progressLine(p, {paint, survey, order}, p.info.statusDate);
        QCOMPARE(line.size(), 3);
        QVERIFY(line.at(1).bent);
        QCOMPARE(toIso(line.at(1).at), toIso(at(2, 17)));
        QVERIFY(line.at(2).bent);
        QCOMPARE(toIso(line.at(2).at), toIso(at(3)));
        GanttPrintOptions withLine = o;
        withLine.showProgressLine = true;
        const QImage drawn = rasterPages(ganttLayout(p, withLine), kLetterLandscape, 144).first();
        grab(drawn, s("gantt_m83_progress_line"));
        const PlanPaperStyle style;
        QVERIFY2(pixels(drawn, style.warning, 40) > pixels(split, style.warning, 40) + 40,
                 qPrintable(QString::number(pixels(drawn, style.warning, 40))));
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PlanOutputM83Test test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plan_output_m83.moc"
