// SPDX-License-Identifier: GPL-3.0-or-later
// PROBE (M88, D-096): the QTextDocumentLayout facts Word sections in
// QindaWrite build on, written before the section layout and kept as its
// regression test. Verified on Qt 6.11.1 (qtextdocumentlayout.cpp,
// qtextcursor.cpp):
//
// Child frames (the design the milestone started from):
//  - a child QTextFrame narrower than the root (QTextFrameFormat::setWidth)
//    wraps its lines at its own width;
//  - child frames paginate across pages with the document's page height,
//    each page's band inset by the root's margins;
//  - blockBoundingRect() includes every enclosing frame's offset, so
//    positions map — but its top is where the block's layout STARTED: a
//    first line Qt pushed to the next page keeps that position and gets a
//    larger y of its own (QTextLayout::boundingRect is moved, not united),
//    so where text is on a page is the block's top plus line.y();
//  - a frame's own top margin narrows the page band of every page it runs
//    over, at the top AND the bottom (Qt adds the child's top margin to the
//    effective bottom margin too);
//  - BUT a selection from one frame into a sibling frame is widened to both
//    whole frames (QTextCursorPrivate::adjustCursor), and in paged mode Qt
//    lays every child frame out again on each pass (layoutFlow forces
//    sizeDirty). Sections as frames would make selecting, formatting or
//    deleting across a section break take both whole sections, so the
//    section layout does NOT use frames.
//
// What it uses instead (write_sections.h, write_pagination.h):
//  - a block's right margin narrows its lines to a section's text width;
//  - a table's right margin narrows a 100 % (or automatic) table the same;
//  - a page break AFTER the last block of a cell in one row moves the next
//    row to the next page, below the table's repeated header rows (the
//    row before stretches to the page bottom, as Qt draws any row it moves
//    itself); a break BEFORE the first blocks of a row's cells moves the
//    row two pages (Qt drops the row, then lays the cell out again with
//    its break), so the section layout never uses that;
//  - a break after one block and a break before the next leave a blank
//    page between them (Word's odd / even page section start).

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>
#include <QtTest>

#include <cmath>

namespace {

constexpr qreal kPageW = 800.0;
constexpr qreal kPageH = 1000.0;
constexpr qreal kMargin = 100.0; // all four sides: a 600 × 800 text area

void setupPaged(QTextDocument *doc)
{
    doc->setUseDesignMetrics(true);
    doc->setPageSize(QSizeF(kPageW, kPageH));
    QTextFrameFormat root = doc->rootFrame()->frameFormat();
    root.setMargin(0);
    root.setLeftMargin(kMargin);
    root.setRightMargin(kMargin);
    root.setTopMargin(kMargin);
    root.setBottomMargin(kMargin);
    doc->rootFrame()->setFrameFormat(root);
}

QString words(int count)
{
    QStringList out;
    for (int i = 0; i < count; ++i) {
        out.append(QStringLiteral("word%1").arg(i % 10));
    }
    return out.join(QLatin1Char(' '));
}

int pageOf(qreal y)
{
    return int(std::floor(y / kPageH));
}

// Where a line of `block` is in the document: the block's layout position
// plus the line's own y (see the note on blockBoundingRect above).
QRectF lineRect(const QTextDocument &doc, const QTextBlock &block, int line)
{
    const QRectF box = doc.documentLayout()->blockBoundingRect(block);
    const QTextLine l = block.layout()->lineAt(line);
    return {box.left() + l.x(), box.top() + l.y(), l.width(), l.height()};
}

// Every line of `block` within `width` (a little slack for rounding).
bool linesFit(const QTextBlock &block, qreal width)
{
    const QTextLayout *layout = block.layout();
    if (layout == nullptr || layout->lineCount() == 0) {
        return false;
    }
    for (int i = 0; i < layout->lineCount(); ++i) {
        if (layout->lineAt(i).width() > width + 0.5 || layout->lineAt(i).naturalTextWidth() > width + 0.5) {
            return false;
        }
    }
    return true;
}

// Fills the frame the cursor is in with `paragraphs` paragraphs.
void fill(QTextCursor &cursor, int paragraphs, const QString &text)
{
    for (int i = 0; i < paragraphs; ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        cursor.insertText(text);
    }
}

// Moves `cursor` to the root block after `frame`.
void leave(QTextCursor &cursor, const QTextFrame *frame)
{
    cursor.setPosition(frame->lastPosition() + 1);
}

} // namespace

class WriteProbeSections : public QObject {
    Q_OBJECT
private slots:
    void childFrameWrapsAtItsOwnWidth();
    void childFramesPaginateAcrossPages();
    void positionsIncludeFrameOffsets();
    void frameTopMarginNarrowsEveryPageBand();
    void selectionAcrossFramesTakesWholeFrames();
    void blockRightMarginNarrowsLines();
    void tableRightMarginNarrowsTable();
    void cellBreaksMoveARowBelowRepeatedHeaders();
    void breakAfterAndBeforeLeaveABlankPage();
};

void WriteProbeSections::childFrameWrapsAtItsOwnWidth()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    cursor.insertText(QStringLiteral("before"));
    QTextFrameFormat narrow;
    narrow.setWidth(QTextLength(QTextLength::FixedLength, 300));
    QTextFrame *a = cursor.insertFrame(narrow);
    cursor.insertText(words(120));
    leave(cursor, a);
    QTextFrameFormat wide;
    wide.setWidth(QTextLength(QTextLength::FixedLength, 600));
    QTextFrame *b = cursor.insertFrame(wide);
    cursor.insertText(words(120));
    doc.documentLayout()->documentSize();

    const QTextBlock inA = doc.findBlock(a->firstPosition());
    const QTextBlock inB = doc.findBlock(b->firstPosition());
    QVERIFY(linesFit(inA, 300));
    QVERIFY(!linesFit(inB, 300));
    QVERIFY(linesFit(inB, 600));
    QVERIFY(inA.layout()->lineCount() > inB.layout()->lineCount());
    QCOMPARE(doc.documentLayout()->frameBoundingRect(a).width(), 300.0);
}

void WriteProbeSections::childFramesPaginateAcrossPages()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextFrameFormat narrow;
    narrow.setWidth(QTextLength(QTextLength::FixedLength, 300));
    QTextFrame *a = cursor.insertFrame(narrow);
    fill(cursor, 120, QStringLiteral("a paragraph of the narrow frame"));
    doc.documentLayout()->documentSize();
    QVERIFY(doc.pageCount() >= 3);

    QSet<int> pages;
    bool pushed = false;
    for (QTextBlock block = doc.findBlock(a->firstPosition()); block.isValid() && block.position() < a->lastPosition();
         block = block.next()) {
        for (int i = 0; i < block.layout()->lineCount(); ++i) {
            const QRectF rect = lineRect(doc, block, i);
            const int page = pageOf(rect.top());
            pages.insert(page);
            QVERIFY2(rect.top() >= page * kPageH + kMargin - 0.5,
                     qPrintable(QStringLiteral("line at %1 above page %2's band").arg(rect.top()).arg(page)));
            QVERIFY2(rect.bottom() <= (page + 1) * kPageH - kMargin + 0.5,
                     qPrintable(QStringLiteral("line ending %1 below page %2's band").arg(rect.bottom()).arg(page)));
            // A pushed first line: the block's box still starts on the page before.
            pushed = pushed || pageOf(doc.documentLayout()->blockBoundingRect(block).top()) < page;
        }
    }
    QVERIFY(pages.size() >= 3);
    QVERIFY(pushed);
}

void WriteProbeSections::positionsIncludeFrameOffsets()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextFrameFormat inset;
    inset.setLeftMargin(50);
    QTextFrame *a = cursor.insertFrame(inset);
    cursor.insertText(QStringLiteral("inset"));
    doc.documentLayout()->documentSize();
    const QRectF rect = doc.documentLayout()->blockBoundingRect(doc.findBlock(a->firstPosition()));
    QCOMPARE(rect.left(), kMargin + 50);
    QVERIFY(rect.top() >= kMargin - 0.5);
}

void WriteProbeSections::frameTopMarginNarrowsEveryPageBand()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextFrameFormat banded;
    banded.setTopMargin(60);
    banded.setBottomMargin(60);
    QTextFrame *a = cursor.insertFrame(banded);
    fill(cursor, 150, QStringLiteral("a paragraph in a frame with its own top and bottom margins"));
    doc.documentLayout()->documentSize();
    QVERIFY(doc.pageCount() >= 3);
    bool sawPageStart = false;
    for (QTextBlock block = doc.findBlock(a->firstPosition()); block.isValid() && block.position() < a->lastPosition();
         block = block.next()) {
        for (int i = 0; i < block.layout()->lineCount(); ++i) {
            const QRectF rect = lineRect(doc, block, i);
            const int page = pageOf(rect.top());
            QVERIFY(rect.top() >= page * kPageH + kMargin + 60 - 0.5);
            QVERIFY(rect.bottom() <= (page + 1) * kPageH - kMargin - 60 + 0.5);
            sawPageStart = sawPageStart || (page > 0 && qAbs(rect.top() - (page * kPageH + kMargin + 60)) < 1);
        }
    }
    QVERIFY(sawPageStart);
}

void WriteProbeSections::selectionAcrossFramesTakesWholeFrames()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextFrame *a = cursor.insertFrame(QTextFrameFormat());
    cursor.insertText(QStringLiteral("first section text"));
    leave(cursor, a);
    QTextFrame *b = cursor.insertFrame(QTextFrameFormat());
    cursor.insertText(QStringLiteral("second section text"));

    // From the middle of one "section" into the middle of the next.
    QTextCursor selection(&doc);
    selection.setPosition(a->firstPosition() + 6);
    selection.setPosition(b->firstPosition() + 6, QTextCursor::KeepAnchor);
    QVERIFY(selection.selectionStart() < a->firstPosition());
    QVERIFY(selection.selectionEnd() > b->lastPosition());
    QVERIFY(selection.selectedText().contains(QStringLiteral("first section text")));
    QVERIFY(selection.selectedText().contains(QStringLiteral("second section text")));

    // The same selection over plain blocks stays where it was put.
    QTextDocument flat;
    QTextCursor plain(&flat);
    plain.insertText(QStringLiteral("first section text"));
    plain.insertBlock();
    plain.insertText(QStringLiteral("second section text"));
    QTextCursor narrow(&flat);
    narrow.setPosition(6);
    narrow.setPosition(flat.findBlockByNumber(1).position() + 6, QTextCursor::KeepAnchor);
    QCOMPARE(narrow.selectionStart(), 6);
    QCOMPARE(narrow.selectionEnd(), flat.findBlockByNumber(1).position() + 6);
}

void WriteProbeSections::blockRightMarginNarrowsLines()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextBlockFormat padded;
    padded.setRightMargin(300);
    cursor.setBlockFormat(padded);
    cursor.insertText(words(120));
    cursor.insertBlock(QTextBlockFormat());
    cursor.insertText(words(120));
    doc.documentLayout()->documentSize();
    QVERIFY(linesFit(doc.begin(), 300));
    QVERIFY(!linesFit(doc.begin().next(), 300));
    QVERIFY(linesFit(doc.begin().next(), 600));
}

void WriteProbeSections::tableRightMarginNarrowsTable()
{
    for (const bool percentage : {true, false}) {
        QTextDocument doc;
        setupPaged(&doc);
        QTextCursor cursor(&doc);
        QTextTableFormat format;
        if (percentage) {
            format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        }
        format.setRightMargin(200);
        QTextTable *table = cursor.insertTable(2, 3, format);
        table->cellAt(0, 2).firstCursorPosition().insertText(words(40));
        doc.documentLayout()->documentSize();
        const QRectF last = doc.documentLayout()->blockBoundingRect(table->cellAt(0, 2).firstCursorPosition().block());
        QVERIFY2(last.right() <= kMargin + 400 + 1, qPrintable(QStringLiteral("right edge %1").arg(last.right())));
        QVERIFY(last.right() > kMargin + 300);
    }
}

void WriteProbeSections::cellBreaksMoveARowBelowRepeatedHeaders()
{
    for (const bool after : {true, false}) {
        QTextDocument doc;
        setupPaged(&doc);
        QTextCursor cursor(&doc);
        cursor.insertText(QStringLiteral("before"));
        QTextTableFormat format;
        format.setHeaderRowCount(1);
        QTextTable *table = cursor.insertTable(8, 2, format);
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 2; ++c) {
                table->cellAt(r, c).firstCursorPosition().insertText(QStringLiteral("r%1c%2").arg(r).arg(c));
            }
        }
        auto rowTop = [&](int row) {
            return lineRect(doc, table->cellAt(row, 0).firstCursorPosition().block(), 0).top();
        };
        doc.documentLayout()->documentSize();
        QCOMPARE(pageOf(rowTop(5)), 0);
        const qreal header = rowTop(1) - rowTop(0);
        QVERIFY(header > 0);

        if (after) {
            // The design's row break: after the last block of a cell of row 4.
            QTextCursor last = table->cellAt(4, 0).lastCursorPosition();
            QTextBlockFormat brk = last.blockFormat();
            brk.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysAfter);
            last.setBlockFormat(brk);
        } else {
            for (int c = 0; c < 2; ++c) {
                QTextCursor first = table->cellAt(5, c).firstCursorPosition();
                QTextBlockFormat brk = first.blockFormat();
                brk.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                first.setBlockFormat(brk);
            }
        }
        doc.documentLayout()->documentSize();
        QCOMPARE(pageOf(rowTop(4)), 0);
        if (!after) {
            QCOMPARE(pageOf(rowTop(5)), 2); // the trap: two pages on
            continue;
        }
        QCOMPARE(pageOf(rowTop(5)), 1);
        // Below the header row Qt repeats at the top of the new page.
        QVERIFY2(rowTop(5) >= kPageH + kMargin + header - 1, qPrintable(QString::number(rowTop(5))));
        QCOMPARE(pageOf(rowTop(6)), 1);
        QCOMPARE(doc.pageCount(), 2);
    }
}

void WriteProbeSections::breakAfterAndBeforeLeaveABlankPage()
{
    QTextDocument doc;
    setupPaged(&doc);
    QTextCursor cursor(&doc);
    QTextBlockFormat after;
    after.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysAfter);
    cursor.setBlockFormat(after);
    cursor.insertText(QStringLiteral("end of a section"));
    QTextBlockFormat before;
    before.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(before);
    cursor.insertText(QStringLiteral("an odd-page section starts here"));
    doc.documentLayout()->documentSize();
    QCOMPARE(doc.pageCount(), 3);
    const QRectF second = doc.documentLayout()->blockBoundingRect(doc.begin().next());
    QCOMPARE(pageOf(second.top()), 2);
    QCOMPARE(second.top(), 2 * kPageH + kMargin);

    // Either break alone starts the next page only.
    QTextCursor undoAfter(doc.begin());
    undoAfter.setBlockFormat(QTextBlockFormat());
    doc.documentLayout()->documentSize();
    QCOMPARE(doc.pageCount(), 2);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WriteProbeSections test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_probe_sections.moc"
