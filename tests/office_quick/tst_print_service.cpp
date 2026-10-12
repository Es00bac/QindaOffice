// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_service.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPrinter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Quick;

// The print service with no QtWidgets application (conversion plan §3.7): a job
// renders its pages through the app's callback into a PDF, the preview is
// rendered from that same PDF (Poppler) when available, and the printer
// model lists what CUPS knows without crashing where it knows nothing.
class PrintServiceTest final : public QObject {
    Q_OBJECT

    // Two pages; honours the printer's page range like an app would.
    static PrintRenderCallback twoPages()
    {
        return [](QPrinter *printer, QPainter *painter) {
            const int first = printer->fromPage() > 0 ? printer->fromPage() : 1;
            const int last = printer->toPage() > 0 ? qMin(printer->toPage(), 2) : 2;
            for (int page = first; page <= last; ++page) {
                if (page != first) {
                    printer->newPage();
                }
                painter->fillRect(QRectF(10, 10, 200, 100), page == 1 ? Qt::red : Qt::blue);
            }
        };
    }

private slots:
    void exportsThePagesTheCallbackPaints()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PrintJob job;
        job.setRender(twoPages());
        const QString path = dir.path() + QStringLiteral("/out.pdf");
        QVERIFY2(job.exportPdf(path), qPrintable(job.lastError()));
        QVERIFY(QFileInfo(path).size() > 0);
        QVERIFY(job.lastError().isEmpty());
    }

    void previewComesFromTheSamePdf()
    {
        PrintJob job;
        job.setRender(twoPages());
        if (!job.previewAvailable()) {
            QSKIP("built without Poppler: no preview");
        }
        QSignalSpy pages(&job, &PrintJob::pagesChanged);
        QVERIFY2(job.preparePreview(), qPrintable(job.lastError()));
        QCOMPARE(job.pageCount(), 2);
        QVERIFY(pages.count() >= 1);
        const QImage first = job.page(0, 72.0);
        const QImage second = job.page(1, 72.0);
        QVERIFY(!first.isNull() && !second.isNull());
        QVERIFY2(first.width() > 60 && first.height() > 60, qPrintable(QString::number(first.width())));
        // Page 1 is red, page 2 blue: the preview shows the rendered pages,
        // not a placeholder.
        QVERIFY(first.pixelColor(30, 30).red() > 200);
        QVERIFY(second.pixelColor(30, 30).blue() > 200);
        // A page range narrows what is rendered.
        job.setFromPage(2);
        job.setToPage(2);
        QVERIFY(job.preparePreview());
        QCOMPARE(job.pageCount(), 1);
        QVERIFY(job.page(0, 72.0).pixelColor(30, 30).blue() > 200);
    }

    void printerModelListsWithoutCrashing()
    {
        PrinterModel model;
        model.refresh();
        QVERIFY(model.rowCount() >= 0);
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(!model.nameAt(row).isEmpty());
        }
        QVERIFY(model.defaultIndex() < model.rowCount());
    }

    void settingsNotifyAndAMissingPrinterFailsVisibly()
    {
        PrintJob job;
        QSignalSpy settings(&job, &PrintJob::settingsChanged);
        job.setCopies(3);
        job.setLandscape(true);
        job.setPrinterName(QStringLiteral("no-such-printer"));
        QCOMPARE(job.copies(), 3);
        QVERIFY(job.landscape());
        QVERIFY(settings.count() >= 3);
        bool rendered = false;
        job.setRender([&rendered](QPrinter *, QPainter *) { rendered = true; });
        QVERIFY(!job.print());
        QVERIFY(job.lastError().contains(QStringLiteral("no-such-printer")));
        QVERIFY(!rendered);
    }

    void requiresAnExplicitPrinter()
    {
        PrintJob job;
        bool rendered = false;
        job.setRender([&rendered](QPrinter *, QPainter *) { rendered = true; });
        QVERIFY(!job.print());
        QVERIFY(job.lastError().contains(QStringLiteral("Choose a printer")));
        QVERIFY(!rendered);
    }

    void rejectsAnInvertedRangeBeforeRendering()
    {
        PrintJob job;
        bool rendered = false;
        job.setRender([&rendered](QPrinter *, QPainter *) { rendered = true; });
        job.setFromPage(3);
        job.setToPage(2);
        QTemporaryDir dir;
        QVERIFY(!job.exportPdf(dir.filePath(QStringLiteral("invalid.pdf"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("invalid.pdf"))));
        QVERIFY(!job.preparePreview());
        QVERIFY(!job.print());
        QVERIFY(!rendered);
    }

    void finalizationMustSucceedBeforeReportingSuccess()
    {
        PrintJob job;
        job.setRender([](QPrinter *, QPainter *painter) { painter->end(); });
        QTemporaryDir dir;
        QVERIFY(!job.exportPdf(dir.filePath(QStringLiteral("ended.pdf"))));
        QVERIFY(!job.lastError().isEmpty());
        QVERIFY(!job.preparePreview());
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A regression in the missing-name guard must never reach a live default.
    QTemporaryDir printerFixture;
    qputenv("CUPS_SERVER", printerFixture.filePath(QStringLiteral("no-cups.sock")).toUtf8());
    QGuiApplication app(argc, argv);
    PrintServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_print_service.moc"
