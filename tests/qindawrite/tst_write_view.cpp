// SPDX-License-Identifier: GPL-3.0-or-later
// M4 acceptance (plan section 8): a 3-page document paginates identically
// on screen and in the exported PDF (per-page text verified through
// poppler); typing reaches the document; zoom changes geometry without
// relayout; named styles classify after an HTML round trip.

#include "qindawrite/src/page_geometry.h"
#include "qindawrite/src/page_text_view.h"
#include "qindawrite/src/pdf_exporter.h"
#include "qindawrite/src/style_catalog.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_window.h"

#include <QAccessible>
#include <QApplication>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QtTest>

#include <poppler-qt6.h>

using namespace QindaOffice::Apps::Write;

class WriteViewTest : public QObject {
    Q_OBJECT
private slots:
    void screenMatchesPdfPagination();
    void typingReachesDocument();
    void zoomTransformsGeometryOnly();
    void stylesSurviveHtmlRoundTrip();
    void geometryRoundTripsInsidePages();
    void pageViewExposesAccessibleText();
    void headerFooterAppearInExportedPdf();

};

static WriteDocument *threePageDocument(QObject *parent, QStringList *markers)
{
    auto *doc = new WriteDocument;
    doc->setParent(parent);
    QTextCursor cursor(doc->textDocument());
    QTextBlockFormat pageBreak;
    pageBreak.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    *markers = {QStringLiteral("alpha marker"), QStringLiteral("bravo marker"),
                QStringLiteral("charlie marker")};
    for (int i = 0; i < markers->size(); ++i) {
        if (i > 0) {
            cursor.insertBlock(pageBreak);
        }
        cursor.insertText(markers->at(i));
    }
    return doc;
}

void WriteViewTest::screenMatchesPdfPagination()
{
    QStringList markers;
    WriteDocument *doc = threePageDocument(this, &markers);
    WriteWindow window;
    // Show the real window offscreen so the view computes with a mapped
    // viewport, then compare its page count with the PDF.
    window.writeDocument()->textDocument()->setHtml(doc->textDocument()->toHtml());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.pageView()->pageCount(), 3);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/m4.pdf");
    QString error;
    QVERIFY2(exportDocumentPdf(window.writeDocument()->textDocument(), path, &error),
             qPrintable(error));

    std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
    QVERIFY(pdf != nullptr);
    QCOMPARE(pdf->numPages(), 3);
    for (int page = 0; page < 3; ++page) {
        std::unique_ptr<Poppler::Page> pdfPage(pdf->page(page));
        QVERIFY(pdfPage != nullptr);
        const QString text = pdfPage->text(QRectF());
        QVERIFY2(text.contains(markers.at(page)),
                 qPrintable(QStringLiteral("page %1 lacks its marker; text: %2")
                                .arg(page)
                                .arg(text)));
    }
}

void WriteViewTest::typingReachesDocument()
{
    WriteWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    PageTextView *view = window.pageView();
    view->setFocus();
    for (const QChar c : QStringLiteral("hello m4")) {
        QTest::keyClick(view, c.toLatin1());
    }
    QCOMPARE(window.writeDocument()->textDocument()->toPlainText(),
             QStringLiteral("hello m4"));
}

void WriteViewTest::zoomTransformsGeometryOnly()
{
    WriteWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    PageTextView *view = window.pageView();
    const QRectF before = view->geometry()->pageRect(0, view->viewport()->width());
    view->setZoom(2.0);
    const QRectF after = view->geometry()->pageRect(0, view->viewport()->width());
    QVERIFY(qFuzzyCompare(after.height(), before.height() * 2.0));
    // No relayout: the document's layout size must not change with zoom.
    QCOMPARE(view->pageCount(), 1);
    QVERIFY(view->geometry()->zoom() > 1.5);
}

void WriteViewTest::stylesSurviveHtmlRoundTrip()
{
    auto *doc = new WriteDocument;
    doc->setParent(this);
    QTextCursor cursor(doc->textDocument());
    StyleCatalog catalog;

    QTextBlock block = doc->textDocument()->begin();
    for (int id = StyleCatalog::Title; id <= StyleCatalog::Code; ++id) {
        QTextCursor blockCursor(block);
        catalog.apply(blockCursor, id);
        block = block.next();
        if (!block.isValid()) {
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock();
            block = doc->textDocument()->lastBlock();
        }
    }
    // Every styled block classifies back to its id after setHtml (the
    // property does not survive Qt's writer; the resolver re-derives it).
    doc->textDocument()->setHtml(doc->textDocument()->toHtml());
    block = doc->textDocument()->begin();
    for (int id = StyleCatalog::Title; id <= StyleCatalog::Code && block.isValid();
         ++id, block = block.next()) {
        QCOMPARE(classifyBlockStyle(block), id);
    }
}

void WriteViewTest::geometryRoundTripsInsidePages()
{
    PageGeometry geometry;
    geometry.setZoom(1.5);
    for (const QPointF docPoint : {QPointF(100, 50), QPointF(400, 1123 + 30), QPointF(0, 0)}) {
        const QPointF view = geometry.docToView(docPoint, 1200);
        const QPointF back = geometry.viewToDoc(view, 1200);
        QVERIFY2(std::abs(back.x() - docPoint.x()) < 1.5
                     && std::abs(back.y() - docPoint.y()) < 1.5,
                 qPrintable(QStringLiteral("%1 -> %2 -> %3")
                                .arg(docPoint.x())
                                .arg(docPoint.y())
                                .arg(back.y())));
    }
    QCOMPARE(geometry.pageCount(1123 * 2.5), 3);
    QVERIFY(geometry.scrollRange(1123 * 2.5, 800) > 0);
}

void WriteViewTest::pageViewExposesAccessibleText()
{
    WriteWindow window;
    PageTextView *view = window.pageView();
    view->writeDocument()->textDocument()->setPlainText(QStringLiteral("Hello QindaWrite"));
    const QString plain = view->writeDocument()->textDocument()->toPlainText();
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(view);
    QVERIFY(iface != nullptr);
    QCOMPARE(iface->role(), QAccessible::Document);
    auto *textIface = iface->textInterface();
    QVERIFY(textIface != nullptr);
    QCOMPARE(textIface->characterCount(), plain.size());
    QCOMPARE(textIface->text(0, 5), plain.left(5));
    QCOMPARE(iface->text(QAccessible::Value), plain);
    textIface->setCursorPosition(plain.size());
    QCOMPARE(textIface->cursorPosition(), plain.size());
    textIface->addSelection(0, 5);
    QCOMPARE(textIface->selectionCount(), 1);
    int selStart = -1;
    int selEnd = -1;
    textIface->selection(0, &selStart, &selEnd);
    QCOMPARE(selStart, 0);
    QCOMPARE(selEnd, 5);
    // Qt's accessibility cache owns interfaces returned by
    // queryAccessibleInterface; do not delete or re-register them.
    Q_UNUSED(iface);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    WriteViewTest test;
    return QTest::qExec(&test, argc, argv);
}

void WriteViewTest::headerFooterAppearInExportedPdf()
{
    WriteWindow window;
    window.writeDocument()->textDocument()->setHtml(
        QStringLiteral("<p>body one</p><p style=\"page-break-before:always\">body two</p>"));
    window.writeDocument()->setHeaderFooter(QStringLiteral("Acme Report"),
                                            QStringLiteral("Page {page} of {pages}"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.pageView()->pageCount(), 2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/hf.pdf");
    QString error;
    QVERIFY2(exportDocumentPdf(window.writeDocument()->textDocument(), path, &error,
                               window.writeDocument()->decoration()),
             qPrintable(error));

    std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
    QVERIFY(pdf != nullptr);
    QCOMPARE(pdf->numPages(), 2);
    for (int page = 0; page < 2; ++page) {
        std::unique_ptr<Poppler::Page> pdfPage(pdf->page(page));
        QVERIFY(pdfPage != nullptr);
        const QString text = pdfPage->text(QRectF());
        QVERIFY2(text.contains(QStringLiteral("Acme Report")),
                 qPrintable(QStringLiteral("header on page %1").arg(page)));
        QVERIFY2(text.contains(QStringLiteral("Page %1 of 2").arg(page + 1)),
                 qPrintable(QStringLiteral("footer on page %1: %2").arg(page).arg(text)));
    }
}

#include "tst_write_view.moc"
