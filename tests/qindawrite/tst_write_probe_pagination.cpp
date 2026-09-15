// SPDX-License-Identifier: GPL-3.0-or-later
// PROBE #4 (plan section 10): QTextDocumentLayout paged-mode facts the
// PageTextView design builds on. Written BEFORE the view; failures here
// changed the design. Verified facts (Qt 6.11.1):
//  - documentSize().height() is CONTENT height (incl. margins), not a page
//    multiple: pageCount = max(1, ceil(height / pageH)).
//  - Root-frame format margins are IGNORED by the paged layout.
//  - setDocumentMargin() applies per page on all four sides (symmetric
//    margins only) - PageTextView page setup is symmetric in v1.
//  - Blocks past page 1 sit at y >= pageH; every block is within pageCount.
//  - Tables split across pages; explicit PageBreak_AlwaysBefore works.
//  - Painting the layout onto a QPdfWriter paginates identically with the
//    screen page count.

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QtTest>

namespace {

constexpr qreal kPageW = 794.0;  // A4 @ 96 dpi
constexpr qreal kPageH = 1123.0;

void setupPaged(QTextDocument *doc, qreal margin)
{
    doc->setUseDesignMetrics(true);
    doc->setDocumentMargin(margin);
    doc->setPageSize(QSizeF(kPageW, kPageH));
}

int pageCountOf(const QTextDocument *doc)
{
    return qMax(1, int(std::ceil(doc->documentLayout()->documentSize().height() / kPageH)));
}

} // namespace

class WriteProbePagination : public QObject {
    Q_OBJECT
private slots:
    void pagedModeBasics();
    void blocksLandInExpectedPages();
    void tablesSplitAcrossPages();
    void explicitPageBreaks();
    void pdfMatchesScreenPagination();
};

void WriteProbePagination::pagedModeBasics()
{
    QTextDocument doc;
    setupPaged(&doc, 96);
    doc.setPlainText(QStringLiteral("hello"));

    // Content height includes the per-page margins: 96 + 14 + 96.
    QCOMPARE(doc.documentLayout()->documentSize().height(), 206.0);
    QCOMPARE(pageCountOf(&doc), 1);

    // documentMargin acts per page on all four sides.
    const QRectF first = doc.documentLayout()->blockBoundingRect(doc.begin());
    QVERIFY(first.top() >= 96 - 1);
    QVERIFY(first.left() >= 96 - 1);
    QVERIFY(first.right() <= kPageW - 96 + 1);
}

void WriteProbePagination::blocksLandInExpectedPages()
{
    QTextDocument doc;
    setupPaged(&doc, 50);
    QStringList lines;
    for (int i = 0; i < 200; ++i) {
        lines.append(QStringLiteral("line %1").arg(i));
    }
    doc.setPlainText(lines.join(QLatin1Char('\n')));
    const int pages = pageCountOf(&doc);
    QVERIFY(pages >= 3);

    QMap<int, int> perPage;
    bool sawSecondBand = false;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QRectF rect = doc.documentLayout()->blockBoundingRect(block);
        const int page = int(rect.top() / kPageH);
        QVERIFY2(page >= 0 && page < pages,
                 qPrintable(QStringLiteral("block at y %1 outside %2 pages")
                                .arg(rect.top())
                                .arg(pages)));
        perPage[page] += 1;
        if (page >= 1) {
            sawSecondBand = true;
        }
    }
    QVERIFY(sawSecondBand);
    QVERIFY(perPage.value(0) > 0);
}

void WriteProbePagination::tablesSplitAcrossPages()
{
    QTextDocument doc;
    setupPaged(&doc, 40);
    QTextCursor cursor(&doc);
    cursor.insertText(QStringLiteral("before\n"));
    QTextTableFormat tableFormat;
    tableFormat.setHeaderRowCount(1);
    QTextTable *table = cursor.insertTable(60, 3, tableFormat);
    for (int r = 0; r < 60; ++r) {
        for (int c = 0; c < 3; ++c) {
            table->cellAt(r, c).firstCursorPosition().insertText(
                QStringLiteral("r%1c%2").arg(r).arg(c));
        }
    }
    QVERIFY(pageCountOf(&doc) >= 2);
}

void WriteProbePagination::explicitPageBreaks()
{
    QTextDocument doc;
    setupPaged(&doc, 0);
    QTextCursor cursor(&doc);
    cursor.insertText(QStringLiteral("page one"));
    QTextBlockFormat pageBreak;
    pageBreak.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(pageBreak);
    cursor.insertText(QStringLiteral("page two"));
    cursor.insertBlock(pageBreak);
    cursor.insertText(QStringLiteral("page three"));
    QCOMPARE(pageCountOf(&doc), 3);
}

void WriteProbePagination::pdfMatchesScreenPagination()
{
    QTextDocument doc;
    setupPaged(&doc, 50);
    QStringList lines;
    for (int i = 0; i < 200; ++i) {
        lines.append(QStringLiteral("probe line %1").arg(i));
    }
    doc.setPlainText(lines.join(QLatin1Char('\n')));
    const int screenPages = pageCountOf(&doc);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/probe.pdf");
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(210, 297), QPageSize::Millimeter));
    writer.setResolution(96);
    // PROBE RESULT: layout->draw() does NOT self-paginate across a
    // QPdfWriter; QTextDocument::print loops pages manually (translate by
    // -i*pageH, clip, draw). The exporter uses the same loop, so screen
    // and PDF paginate identically by construction.
    QPainter painter(&writer);
    const QSizeF paper = writer.pageLayout().paintRectPixels(96).size();
    painter.scale(paper.width() / kPageW, paper.height() / kPageH);
    for (int page = 0; page < screenPages; ++page) {
        painter.save();
        painter.translate(0, -page * kPageH);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(0, page * kPageH, kPageW, kPageH);
        doc.documentLayout()->draw(&painter, context);
        painter.restore();
        if (page + 1 < screenPages) {
            writer.newPage();
        }
    }
    painter.end();

    QFile pdf(path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    const QByteArray bytes = pdf.readAll();
    QVERIFY(bytes.startsWith("%PDF-"));
    QCOMPARE(bytes.count("/Type /Page") - bytes.count("/Type /Pages"), screenPages);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    WriteProbePagination test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_probe_pagination.moc"
