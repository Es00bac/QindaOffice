// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_test_support.h"

#include "print_service.h"
#include "qindawrite/src/write_pagination.h"
#include "support/quick_dump.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTextBlockFormat>
#include <poppler-qt6.h>

using namespace QindaOffice;
using WriteTest::Scene;
using WriteTest::WriteHost;

namespace {

void threePages(WriteHost &host)
{
    QTextCursor cursor(host.writeDocument()->textDocument());
    cursor.insertText(QStringLiteral("PRINT PAGE ONE"));
    QTextBlockFormat next;
    next.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(next);
    cursor.insertText(QStringLiteral("PRINT PAGE TWO"));
    cursor.insertBlock(next);
    cursor.insertText(QStringLiteral("PRINT PAGE THREE"));
    host.writeDocument()->setHeaderFooter({}, QStringLiteral("Page {page} of {pages}"));
    host.writeDocument()->keepRules()->applyAll();
}

} // namespace

class WritePrintTest final : public QObject {
    Q_OBJECT

private slots:
    void selectedPageRetainsItsOriginalNumber()
    {
        WriteHost host;
        threePages(host);
        QCOMPARE(host.writeDocument()->pageFrames().size(), 3);
        Quick::PrintJob *job = host.createPrintJob();
        QVERIFY(job != nullptr);
        job->setFromPage(2);
        job->setToPage(2);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("range.pdf"));
        QVERIFY2(job->exportPdf(path), qPrintable(job->lastError()));
        const auto pdf = Poppler::Document::load(path);
        QVERIFY(pdf != nullptr);
        QCOMPARE(pdf->numPages(), 1);
        const QString text = pdf->page(0)->text({});
        QVERIFY(text.contains(QStringLiteral("PRINT PAGE TWO")));
        QVERIFY(text.contains(QStringLiteral("Page 2 of 3")));
        QVERIFY(!text.contains(QStringLiteral("PRINT PAGE ONE")));
        QVERIFY(!text.contains(QStringLiteral("PRINT PAGE THREE")));
        QVERIFY2(job->preparePreview(), qPrintable(job->lastError()));
        QCOMPARE(job->pageCount(), 1);
    }

    void rangeStartsOnItsSectionsPaper()
    {
        WriteHost host;
        QTextCursor cursor(host.writeDocument()->textDocument());
        cursor.insertText(QStringLiteral("Portrait page"));
        host.control()->setTextCursor(cursor);
        QVERIFY(host.actions()->trigger(QStringLiteral("writeSectionBreakNextPageAction")));
        QVERIFY(host.actions()->trigger(QStringLiteral("writeOrientationLandscapeAction")));
        cursor = host.control()->textCursor();
        cursor.insertText(QStringLiteral("Landscape page"));
        host.writeDocument()->keepRules()->applyAll();
        QCOMPARE(host.writeDocument()->pageFrames().size(), 2);
        const QSizeF expected = host.writeDocument()->pageFrame(1).size * 0.75;
        QVERIFY(expected.width() > expected.height());
        Quick::PrintJob *job = host.createPrintJob();
        QVERIFY(job != nullptr);
        job->setFromPage(2);
        job->setToPage(2);
        QVERIFY(job->landscape());
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("landscape-range.pdf"));
        QVERIFY2(job->exportPdf(path), qPrintable(job->lastError()));
        const auto pdf = Poppler::Document::load(path);
        QVERIFY(pdf != nullptr);
        QCOMPARE(pdf->numPages(), 1);
        const auto page = pdf->page(0);
        QVERIFY(qAbs(page->pageSizeF().width() - expected.width()) < 1);
        QVERIFY(qAbs(page->pageSizeF().height() - expected.height()) < 1);
        QVERIFY(page->text({}).contains(QStringLiteral("Landscape page")));
        QVERIFY(!page->text({}).contains(QStringLiteral("Portrait page")));
    }

    void rejectsRangeBeyondTheDocumentWithoutABlankJob()
    {
        WriteHost host;
        threePages(host);
        Quick::PrintJob *job = host.createPrintJob();
        job->setFromPage(4);
        job->setToPage(5);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("past-end.pdf"));
        QVERIFY(!job->exportPdf(path));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(job->lastError().contains(QStringLiteral("end of the document")));
    }

    void printActionShowsSetupAndRefreshAndPersistentFailure()
    {
        if (!qEnvironmentVariableIsEmpty("QQO_TEST_PRINT_QUEUE")) {
            QSKIP("The hardware opt-in uses live printer discovery; this UI case requires the isolated no-server fixture.");
        }
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("filePrintAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("printDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!dialog->property("primaryEnabled").toBool());
        auto *empty = scene.item(QStringLiteral("printNoPrinters"));
        auto *refresh = scene.item(QStringLiteral("printRefresh"));
        QVERIFY(empty != nullptr && empty->isVisible());
        QVERIFY(empty->property("text").toString().contains(QStringLiteral("printer settings")));
        QVERIFY(refresh != nullptr && refresh->isVisible());
        auto *printers = dialog->findChild<Quick::PrinterModel *>(QStringLiteral("printPrinters"));
        QVERIFY(printers != nullptr);
        QSignalSpy refreshed(printers, &Quick::PrinterModel::countChanged);
        QVERIFY(QMetaObject::invokeMethod(refresh, "clicked"));
        QCOMPARE(refreshed.count(), 1);
        Test::saveGrab(scene.window, QStringLiteral("write-print-520"));
        auto *job = qvariant_cast<Quick::PrintJob *>(dialog->property("job"));
        QVERIFY(job != nullptr);
        job->setPrinterName(QStringLiteral("qindaoffice-missing-test-printer"));
        QVERIFY(!job->print());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "printed", Q_ARG(bool, false)));
        QObject *message = scene.window->findChild<QObject *>(QStringLiteral("messageDialog"));
        QVERIFY(message != nullptr);
        QTRY_VERIFY(message->property("opened").toBool());
        QCOMPARE(message->property("title").toString(), QStringLiteral("Printing failed"));
        QCOMPARE(message->property("text").toString(), job->lastError());
        Test::saveGrab(scene.window, QStringLiteral("write-print-error-520"));
    }

    void hardwarePrinterOptIn()
    {
        const QString queue = qEnvironmentVariable("QQO_TEST_PRINT_QUEUE");
        if (queue.isEmpty() || !QCoreApplication::arguments().contains(QStringLiteral("hardwarePrinterOptIn"))) {
            QSKIP("Physical printing requires explicit QQO_TEST_PRINT_QUEUE and this test name.");
        }
        WriteHost host;
        QTextCursor cursor(host.writeDocument()->textDocument());
        cursor.insertText(QStringLiteral("QindaWrite printer verification\n\n"
                                         "This one-page test was generated by QindaWrite's production document renderer.\n"
                                         "Printer queue: %1\n\n"
                                         "Text, page layout and submission completed through the shared office print service.")
                              .arg(queue));
        host.writeDocument()->keepRules()->applyAll();
        Quick::PrintJob *job = host.createPrintJob();
        QVERIFY(job != nullptr);
        job->setPrinterName(queue);
        job->setFromPage(1);
        job->setToPage(1);
        QVERIFY2(job->print(), qPrintable(job->lastError()));
    }

    void printerDialogReadOnlyOptIn()
    {
        const QString queue = qEnvironmentVariable("QQO_TEST_PRINT_QUEUE");
        if (queue.isEmpty()) {
            QSKIP("Configured-printer UI inspection requires QQO_TEST_PRINT_QUEUE; it never submits a job.");
        }
        Scene scene(QSize(1280, 900));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("filePrintAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("printDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *printers = dialog->findChild<Quick::PrinterModel *>(QStringLiteral("printPrinters"));
        QVERIFY(printers != nullptr);
        int row = -1;
        for (int i = 0; i < printers->rowCount(); ++i) {
            if (printers->nameAt(i) == queue) row = i;
        }
        QVERIFY2(row >= 0, qPrintable(QStringLiteral("Queue unavailable: %1").arg(queue)));
        auto *box = scene.item(QStringLiteral("printPrinter"));
        QVERIFY(box != nullptr);
        box->setProperty("currentIndex", row);
        QVERIFY(dialog->property("primaryEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(scene.item(QStringLiteral("printRefresh")), "clicked"));
        QCOMPARE(printers->nameAt(box->property("currentIndex").toInt()), queue);
        Test::saveGrab(scene.window, QStringLiteral("write-print-configured-1280"));
        scene.window->resize(QSize(520, 800));
        QTest::qWait(80);
        Test::saveGrab(scene.window, QStringLiteral("write-print-configured-520"));
        scene.item(QStringLiteral("printRange"))->setProperty("currentIndex", 1);
        scene.item(QStringLiteral("printFrom"))->setProperty("value", 3);
        scene.item(QStringLiteral("printTo"))->setProperty("value", 2);
        QVERIFY(!dialog->property("primaryEnabled").toBool());
        QVERIFY(scene.item(QStringLiteral("printRangeError"))->isVisible());
        QVERIFY(QMetaObject::invokeMethod(dialog, "refreshPrinters",
                                         Q_ARG(QVariant, QStringLiteral("qindaoffice-removed-test-printer"))));
        QCOMPARE(box->property("currentIndex").toInt(), -1);
        QVERIFY(!dialog->property("primaryEnabled").toBool());
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_STATE_HOME", config.path().toUtf8());
    qputenv("XDG_DATA_HOME", config.path().toUtf8());
    if (qEnvironmentVariableIsEmpty("QQO_TEST_PRINT_QUEUE")) {
        qputenv("CUPS_SERVER", config.filePath(QStringLiteral("no-cups.sock")).toUtf8());
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaWrite"));
    WritePrintTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_print.moc"
