// SPDX-License-Identifier: GPL-3.0-or-later
// plan_output: the Gantt printout's pagination (rows down, timescale
// across), real PDFs of the Gantt and the overview report with the page
// counts the layouts promise, and the Timeline strip as an image and a PNG.
// QQO_GRAB_DIR (+ QQO_GRAB_PREFIX) saves PNG renders of the first pages
// and of the timeline at two sizes for review. M63: the task sheet as xlsx,
// read back with QXlsx — numbers, money, dates and percentages as numbers.

#include "baselines.h"
#include "gantt_print.h"
#include "plan_edit.h"
#include "plan_reports.h"
#include "plan_time.h"
#include "plan_xlsx.h"
#include "report_print.h"
#include "scheduler.h"
#include "timeline_render.h"

#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <xlsxcell.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

using namespace QindaOffice::Plan;

namespace {

constexpr Minutes kDay = 480;
const QSizeF kLetterLandscape(792, 612);
const QSizeF kLetterPortrait(612, 792);

struct Gala {
    Project p = newProject(QDate(2026, 3, 2), QStringLiteral("Spring fundraiser gala"));

    int task(const QString &name, Minutes duration, int level, const QList<int> &preds = {})
    {
        Task t;
        t.uid = p.nextTaskUid();
        t.guid = QUuid::createUuid();
        t.name = name;
        t.duration = duration;
        t.outlineLevel = level;
        for (int pr : preds) {
            t.predecessors.append(Link{p.tasks.at(pr).uid, LinkType::FinishToStart, 0, std::nullopt, {}});
        }
        p.tasks.append(t);
        return int(p.tasks.size()) - 1;
    }
    void names(int index, const QString &text) { QVERIFY(setResourceNames(p, index, text, QLocale::c()).ok); }

    Gala()
    {
        const QLocale c = QLocale::c();
        task(QStringLiteral("Planning"), 0, 1);
        const int kickoff = task(QStringLiteral("Kickoff meeting"), 0, 2);
        const int goals = task(QStringLiteral("Set fundraising goal and budget"), 2 * kDay, 2, {kickoff});
        const int committee = task(QStringLiteral("Recruit host committee"), 5 * kDay, 2, {goals});
        const int theme = task(QStringLiteral("Choose theme and date"), kDay, 2, {goals});
        const int approved = task(QStringLiteral("Plan approved"), 0, 2, {committee, theme});
        task(QStringLiteral("Venue & catering"), 0, 1);
        const int venues = task(QStringLiteral("Shortlist venues"), 3 * kDay, 2, {approved});
        const int visits = task(QStringLiteral("Site visits"), 2 * kDay, 2, {venues});
        const int contract = task(QStringLiteral("Sign venue contract"), kDay, 2, {visits});
        const int caterer = task(QStringLiteral("Caterer tasting and menu"), 3 * kDay, 2, {contract});
        const int av = task(QStringLiteral("Book AV and lighting"), 2 * kDay, 2, {contract});
        const int venueDone = task(QStringLiteral("Venue booked"), 0, 2, {caterer, av});
        task(QStringLiteral("Sponsorship & marketing"), 0, 1);
        const int deck = task(QStringLiteral("Sponsorship deck"), 3 * kDay, 2, {approved});
        const int asks = task(QStringLiteral("Sponsor outreach"), 10 * kDay, 2, {deck});
        const int design = task(QStringLiteral("Invitation design"), 4 * kDay, 2, {theme});
        const int print = task(QStringLiteral("Print and mail invitations"), 3 * kDay, 2, {design, contract});
        const int site = task(QStringLiteral("Event page and ticketing"), 3 * kDay, 2, {design});
        const int press = task(QStringLiteral("Press release"), 2 * kDay, 2, {site});
        task(QStringLiteral("Program"), 0, 1);
        const int auction = task(QStringLiteral("Collect auction items"), 8 * kDay, 2, {approved});
        const int speakers = task(QStringLiteral("Confirm speakers"), 4 * kDay, 2, {approved});
        const int run = task(QStringLiteral("Run of show"), 2 * kDay, 2, {speakers, auction});
        const int volunteers = task(QStringLiteral("Volunteer briefing"), kDay, 2, {run});
        task(QStringLiteral("Event week"), 0, 1);
        const int rehearsal = task(QStringLiteral("Rehearsal"), kDay, 2, {volunteers, venueDone});
        const int gala = task(QStringLiteral("Gala night"), kDay, 2, {rehearsal, print, press, asks});
        const int thanks = task(QStringLiteral("Thank-you notes"), 3 * kDay, 2, {gala});
        task(QStringLiteral("Wrap-up report"), 2 * kDay, 2, {thanks});
        task(QStringLiteral("Gala complete"), 0, 1, {int(p.tasks.size()) - 1});
        p.tasks[visits].predecessors.first().type = LinkType::FinishToStart;
        p.tasks[site].predecessors.first().type = LinkType::StartToStart;
        p.tasks[site].predecessors.first().lag = kDay;
        for (const char *name : {"Dana Whitfield", "Ana Ruiz", "Ben Okafor", "Priya Nair", "Harbor Catering"}) {
            addResource(p, int(p.resources.size()), QString::fromLatin1(name));
        }
        p.resources[1].standardRate = 4500;
        addResource(p, int(p.resources.size()), QStringLiteral("Card stock"), ResourceType::Material);
        p.resources.last().materialLabel = QStringLiteral("sheets");
        names(goals, QStringLiteral("Dana Whitfield"));
        names(committee, QStringLiteral("Dana Whitfield[50%], Priya Nair"));
        names(venues, QStringLiteral("Ana Ruiz"));
        names(visits, QStringLiteral("Ana Ruiz, Dana Whitfield"));
        names(caterer, QStringLiteral("Harbor Catering, Ana Ruiz"));
        names(deck, QStringLiteral("Ben Okafor"));
        names(asks, QStringLiteral("Ben Okafor, Dana Whitfield[25%]"));
        names(design, QStringLiteral("Priya Nair"));
        names(print, QStringLiteral("Priya Nair, Card stock[300 sheets]"));
        names(auction, QStringLiteral("Ana Ruiz[50%]"));
        schedule(p);
        setBaseline(p, 0);
        setPercentComplete(p, {goals, committee, theme}, 100);
        setPercentComplete(p, {venues}, 60);
        p.tasks[visits].duration += 2 * kDay; // slipped against the baseline
        p.info.statusDate = toMinutes(QDate(2026, 3, 13), 17 * 60);
        schedule(p);
        Q_UNUSED(c)
    }
};

int pdfPages(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray bytes = f.readAll();
    if (!bytes.startsWith("%PDF")) {
        return -2;
    }
    static const QRegularExpression re(QStringLiteral("/Type\\s*/Page[^s]"));
    return int(QString::fromLatin1(bytes).count(re));
}

void grab(const QImage &image, const QString &name)
{
    const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
    if (dir.isEmpty()) {
        return;
    }
    QVERIFY(image.save(dir + QLatin1Char('/') + qEnvironmentVariable("QQO_GRAB_PREFIX") + name + QStringLiteral(".png")));
}

GanttPrintOptions options(const Project &p)
{
    GanttPrintOptions o;
    o.locale = QLocale::c();
    o.today = toMinutes(QDate(2026, 3, 11), 12 * 60);
    o.statusDate = p.info.statusDate;
    o.columns = {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("duration"), QStringLiteral("start"),
                 QStringLiteral("finish")};
    return o;
}

} // namespace

class PlanOutputTest final : public QObject {
    Q_OBJECT
private slots:
    void ganttPagination()
    {
        Gala g;
        QCOMPARE(g.p.tasks.size(), 31);
        GanttPrintOptions o = options(g.p);
        QCOMPARE(ganttPageCount(g.p, o, kLetterLandscape), 1);
        o.pagesWide = 2;
        QCOMPARE(ganttPageCount(g.p, o, kLetterLandscape), 2);
        // A long plan: rows paginate down, then across.
        Gala big;
        const QList<Task> copy = big.p.tasks;
        for (int k = 0; k < 2; ++k) {
            for (Task t : copy) {
                t.uid = big.p.nextTaskUid();
                t.guid = QUuid::createUuid();
                t.predecessors.clear();
                big.p.tasks.append(t);
            }
        }
        schedule(big.p);
        GanttPrintOptions bo = options(big.p);
        const int down = ganttPageCount(big.p, bo, kLetterLandscape);
        QVERIFY2(down >= 2, qPrintable(QString::number(down)));
        bo.pagesWide = 3;
        QCOMPARE(ganttPageCount(big.p, bo, kLetterLandscape), down * 3);
        // Collapsing a phase hides its subtasks.
        GanttPrintOptions hidden = options(g.p);
        for (const Task &t : std::as_const(g.p.tasks)) {
            if (t.outlineLevel == 2) {
                hidden.hiddenTaskUids.append(t.uid);
            }
        }
        QCOMPARE(ganttPageCount(g.p, hidden, kLetterPortrait), 1);
    }

    void ganttPdf()
    {
        QTemporaryDir dir;
        Gala g;
        GanttPrintOptions o = options(g.p);
        o.pagesWide = 2;
        const QString path = dir.filePath(QStringLiteral("gantt.pdf"));
        QString error;
        QVERIFY2(writePdf(path, ganttPainter(g.p, o), QPageSize(QPageSize::Letter), QPageLayout::Landscape, &error),
                 qPrintable(error));
        QCOMPARE(pdfPages(path), ganttPageCount(g.p, o, kLetterLandscape));
        const QList<QImage> pages = rasterPages(ganttLayout(g.p, options(g.p)), kLetterLandscape, 144);
        QCOMPARE(pages.size(), 1);
        QVERIFY(!pages.first().isNull());
        grab(pages.first(), QStringLiteral("gantt_page1"));
        const QList<QImage> wide = rasterPages(ganttLayout(g.p, o), kLetterLandscape, 144);
        QCOMPARE(wide.size(), 2);
        grab(wide.at(1), QStringLiteral("gantt_2wide_page2"));
    }

    void reportPdf()
    {
        QTemporaryDir dir;
        Gala g;
        const PlanReport report = buildReport(g.p, QStringLiteral("overview"), QDate(2026, 3, 13), QLocale::c());
        const QString path = dir.filePath(QStringLiteral("overview.pdf"));
        QVERIFY(writePdf(path, reportPainter(report, QLocale::c()), QPageSize(QPageSize::Letter), QPageLayout::Portrait));
        const QList<QImage> pages = rasterPages(reportLayout(report, QLocale::c()), kLetterPortrait, 144);
        QCOMPARE(pdfPages(path), pages.size());
        grab(pages.first(), QStringLiteral("report_overview_page1"));
        // A long table breaks across pages with its header repeated.
        PlanReport longReport = buildReport(g.p, QStringLiteral("critical"), QDate(2026, 3, 13), QLocale::c());
        for (int k = 0; k < 6; ++k) {
            longReport.tables.first().rows += longReport.tables.first().rows;
            longReport.tables.first().rowTones += longReport.tables.first().rowTones;
        }
        const QList<QImage> longPages = rasterPages(reportLayout(longReport, QLocale::c()), kLetterPortrait, 96);
        QVERIFY(longPages.size() >= 3);
        grab(longPages.at(1), QStringLiteral("report_critical_page2"));
        const PlanReport resources = buildReport(g.p, QStringLiteral("resources"), QDate(2026, 3, 13), QLocale::c());
        grab(rasterPages(reportLayout(resources, QLocale::c()), kLetterPortrait, 144).first(),
             QStringLiteral("report_resources_page1"));
    }

    void timeline()
    {
        Gala g;
        TimelineOptions o;
        o.locale = QLocale::c();
        o.today = toMinutes(QDate(2026, 3, 11), 12 * 60);
        const QImage wide = renderTimeline(g.p, QSize(1200, 220), o, 1.0);
        QCOMPARE(wide.size(), QSize(1200, 220));
        // The band spans the width: its middle row is not paper across the
        // middle 70% of the image.
        int band = 0;
        int ink = 0;
        const PlanPaperStyle s;
        for (int y = 0; y < wide.height(); ++y) {
            int nonPaper = 0;
            for (int x = int(wide.width() * 0.15); x < int(wide.width() * 0.85); ++x) {
                const QColor c = wide.pixelColor(x, y);
                nonPaper += c != s.paper ? 1 : 0;
                ink += c == s.ink ? 1 : 0;
            }
            band = std::max(band, nonPaper);
        }
        QVERIFY2(band > wide.width() * 0.69, qPrintable(QString::number(band)));
        QVERIFY(ink > 50); // milestone diamonds and callout text
        grab(wide, QStringLiteral("timeline_1200"));
        const QImage narrow = renderTimeline(g.p, QSize(520, 200), o, 2.0);
        QCOMPARE(narrow.size(), QSize(1040, 400));
        grab(narrow, QStringLiteral("timeline_520"));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("timeline.png"));
        QString error;
        QVERIFY2(exportTimelinePng(g.p, path, QSize(900, 200), o, &error), qPrintable(error));
        QImage back(path);
        QCOMPARE(back.size(), QSize(1800, 400));
        QVERIFY(!exportTimelinePng(g.p, dir.filePath(QStringLiteral("no/such/dir.png")), QSize(10, 10), o, &error));
    }

    void taskSheetToExcel()
    {
        Gala g;
        int venues = -1; // 60 % complete, Ana Ruiz for three days
        for (int i = 0; i < g.p.tasks.size(); ++i) {
            venues = g.p.tasks.at(i).name == QStringLiteral("Shortlist venues") ? i : venues;
        }
        QVERIFY(venues >= 0);
        g.p.tasks[venues].priority = 800;
        schedule(g.p);
        const QStringList fields{QStringLiteral("id"),       QStringLiteral("name"),     QStringLiteral("duration"),
                                 QStringLiteral("start"),    QStringLiteral("percent"),  QStringLiteral("cost"),
                                 QStringLiteral("work"),     QStringLiteral("priority"), QStringLiteral("resources")};
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sheet.xlsx"));
        QString error;
        QVERIFY2(writeTaskSheetXlsx(g.p, fields, path, QLocale::c(), &error), qPrintable(error));
        QXlsx::Document doc(path);
        QVERIFY(doc.isLoadPackage());
        QCOMPARE(doc.read(1, 2).toString(), QStringLiteral("Task Name"));
        QVERIFY(doc.cellAt(1, 2)->format().fontBold());
        const int row = venues + 2;
        QCOMPARE(doc.read(row, 1).toInt(), g.p.tasks.at(venues).id);
        QCOMPARE(doc.read(row, 2).toString().trimmed(), QStringLiteral("Shortlist venues"));
        QVERIFY(doc.read(row, 2).toString().startsWith(QLatin1Char(' '))); // outline level 2
        const std::shared_ptr<QXlsx::Cell> duration = doc.cellAt(row, 3);
        QCOMPARE(duration->value().typeId(), QMetaType::Double);
        QCOMPARE(duration->value().toDouble(), 3.0); // three working days
        const std::shared_ptr<QXlsx::Cell> start = doc.cellAt(row, 4);
        QCOMPARE(start->format().numberFormat(), QStringLiteral("yyyy-mm-dd hh:mm"));
        const Minutes begins = g.p.tasks.at(venues).start;
        QCOMPARE(start->value().toDouble(),
                 double(QDate(1899, 12, 30).daysTo(dateOf(begins))) + minuteOfDay(begins) / 1440.0);
        QCOMPARE(doc.cellAt(row, 5)->value().toDouble(), 0.6);
        // "0%" is Excel's built-in format 9, written by index.
        const QXlsx::Format percent = doc.cellAt(row, 5)->format();
        QVERIFY(percent.numberFormat() == QStringLiteral("0%") || percent.numberFormatIndex() == 9);
        const std::shared_ptr<QXlsx::Cell> cost = doc.cellAt(row, 6);
        QCOMPARE(cost->value().typeId(), QMetaType::Double);
        QCOMPARE(qRound64(cost->value().toDouble() * 100), g.p.tasks.at(venues).cost);
        QVERIFY(cost->format().numberFormat().startsWith(QStringLiteral("\"$\"#,##0.00")));
        QCOMPARE(doc.cellAt(row, 7)->value().toDouble(), double(g.p.tasks.at(venues).work) / 60.0);
        QCOMPARE(doc.read(row, 8).toInt(), 800);
        QCOMPARE(doc.read(row, 9).toString(), QStringLiteral("Ana Ruiz"));
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PlanOutputTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plan_output.moc"
