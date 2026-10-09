// SPDX-License-Identifier: GPL-3.0-or-later
// Word's keep-with-next and widow/orphan rules on Write's pages
// (write_pagination.h, D-068 layout adjustments): a heading at a page
// bottom moves to its text, an orphaned first line moves, the breaks never
// reach the .docx or HTML, and a live pass is invisible to Undo and Redo.

#include "docx_properties.h"
#include "ooxml_package.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_pagination.h"

#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextTable>
#include <QtTest>

#include <cmath>

using QindaOffice::Apps::Write::KeepRules;
using QindaOffice::Apps::Write::WriteDocument;
namespace Docx = QindaOffice::Docx;

namespace {

// The page a block's first (or last) line is on.
int pageOf(const QTextBlock &block, bool last = false)
{
    const QTextDocument *doc = block.document();
    const QTextLayout *layout = block.layout();
    const qreal top = doc->documentLayout()->blockBoundingRect(block).top();
    const QTextLine line = layout->lineAt(last ? layout->lineCount() - 1 : 0);
    return int(std::floor((top + line.y() + line.height() / 2) / doc->pageSize().height()));
}

QTextBlock blockAt(const QTextDocument *doc, int index)
{
    return doc->findBlockByNumber(index);
}

// `count` one-line paragraphs; returns the index of the last one on page 1.
int fillLines(WriteDocument &doc, int count)
{
    QTextDocument *text = doc.textDocument();
    QTextCursor cursor(text);
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        cursor.insertText(QStringLiteral("Line %1").arg(i));
    }
    text->pageCount();
    int last = 0;
    while (pageOf(blockAt(text, last + 1)) == 0) {
        ++last;
    }
    return last;
}

void setKeepWithNext(QTextDocument *text, int index)
{
    QTextBlockFormat format;
    format.setProperty(Docx::kKeepWithNext, true);
    QTextCursor(blockAt(text, index)).mergeBlockFormat(format);
}

// What the document is right after opening: no undo history, unmodified.
void settle(QTextDocument *text)
{
    text->clearUndoRedoStacks();
    text->setModified(false);
}

} // namespace

class WritePaginationTest : public QObject {
    Q_OBJECT
private slots:
    void headingKeepsWithItsText();
    void orphanedFirstLineMoves();
    void tableKeepsItsTitleRowWithTheRest();
    void breaksStayOutOfTheFiles();
    void liveEditsRepaginateWithoutAnUndoStep();
    void longDocumentStaysQuick();
};

void WritePaginationTest::headingKeepsWithItsText()
{
    WriteDocument doc;
    QTextDocument *text = doc.textDocument();
    const int last = fillLines(doc, 120);
    setKeepWithNext(text, last); // the last line of page 1 is now a "heading"
    settle(text);
    doc.keepRules()->applyAll();
    QCOMPARE(pageOf(blockAt(text, last)), 1);
    QCOMPARE(pageOf(blockAt(text, last + 1)), 1);
    QCOMPARE(KeepRules::count(text), 1);
    // The break is the layout's, not the author's: no undo step, not dirty.
    QVERIFY(!text->isUndoAvailable());
    QVERIFY(!text->isModified());
    // A run of keep-with-next paragraphs moves together.
    WriteDocument run;
    const int end = fillLines(run, 120);
    setKeepWithNext(run.textDocument(), end - 1);
    setKeepWithNext(run.textDocument(), end);
    settle(run.textDocument());
    run.keepRules()->applyAll();
    QCOMPARE(pageOf(blockAt(run.textDocument(), end - 1)), 1);
    QCOMPARE(KeepRules::count(run.textDocument()), 1);
}

void WritePaginationTest::orphanedFirstLineMoves()
{
    WriteDocument doc;
    QTextDocument *text = doc.textDocument();
    const int last = fillLines(doc, 120);
    QTextCursor cursor(blockAt(text, last));
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QStringLiteral(" and a sentence that runs on").repeated(12)); // several lines
    settle(text);
    text->pageCount();
    QCOMPARE(pageOf(blockAt(text, last)), 0); // its first line alone ends page 1
    doc.keepRules()->applyAll();
    QCOMPARE(pageOf(blockAt(text, last)), 1);
    QCOMPARE(pageOf(blockAt(text, last), true), 1);
    // Word's widow/orphan control switched off in the file: left alone.
    WriteDocument off;
    const int at = fillLines(off, 120);
    QTextCursor tail(blockAt(off.textDocument(), at));
    tail.movePosition(QTextCursor::EndOfBlock);
    tail.insertText(QStringLiteral(" and a sentence that runs on").repeated(12));
    QTextBlockFormat noControl;
    noControl.setProperty(Docx::kWidowControl, false);
    tail.mergeBlockFormat(noControl);
    settle(off.textDocument());
    off.keepRules()->applyAll();
    QCOMPARE(pageOf(blockAt(off.textDocument(), at)), 0);
}

void WritePaginationTest::tableKeepsItsTitleRowWithTheRest()
{
    // A three-row table whose title row keeps with the next row, placed so
    // that Qt splits it after the title row; the whole table must move.
    for (int back = 1; back <= 4; ++back) {
        WriteDocument doc;
        QTextDocument *text = doc.textDocument();
        const int last = fillLines(doc, 120);
        QTextCursor cursor(blockAt(text, last - back));
        cursor.movePosition(QTextCursor::EndOfBlock);
        QTextTable *table = cursor.insertTable(3, 2);
        table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Title"));
        table->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("Purpose"));
        table->cellAt(2, 0).firstCursorPosition().insertText(QStringLiteral("Who uses it"));
        QTextBlockFormat keep;
        keep.setProperty(Docx::kKeepWithNext, true);
        QTextCursor(table->cellAt(0, 0).firstCursorPosition()).mergeBlockFormat(keep);
        settle(text);
        text->pageCount();
        const QTextBlock title = table->cellAt(0, 0).firstCursorPosition().block();
        const QTextBlock row1 = table->cellAt(1, 0).firstCursorPosition().block();
        if (pageOf(title) != 0 || pageOf(row1) != 1) {
            continue; // this placement does not split after the title row
        }
        doc.keepRules()->applyAll();
        QCOMPARE(pageOf(title), 1);
        QCOMPARE(pageOf(row1), 1);
        QCOMPARE(KeepRules::count(text), 1);
        QVERIFY(table->format().hasProperty(Docx::kSyntheticBreak));
        return;
    }
    QFAIL("no placement split the table after its title row");
}

void WritePaginationTest::breaksStayOutOfTheFiles()
{
    QTemporaryDir dir;
    WriteDocument doc;
    QTextDocument *text = doc.textDocument();
    const int last = fillLines(doc, 120);
    setKeepWithNext(text, last);
    settle(text);
    doc.keepRules()->applyAll();
    QCOMPARE(KeepRules::count(text), 1);

    const QString path = dir.filePath(QStringLiteral("keep.docx"));
    QString error;
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    const auto package = QindaOffice::Ooxml::Package::load(path);
    QVERIFY(package);
    const QByteArray body = package->data(package->mainPart());
    QVERIFY(!body.contains("pageBreakBefore"));
    QVERIFY(!body.contains("w:type=\"page\""));
    QCOMPARE(body.count("<w:keepNext/>"), 1);
    // Reopened, Word's rule is read back and laid out again.
    WriteDocument reopened;
    QVERIFY2(reopened.load(path, &error), qPrintable(error));
    QCOMPARE(KeepRules::count(reopened.textDocument()), 1);
    QCOMPARE(pageOf(blockAt(reopened.textDocument(), last)), 1);
    QVERIFY(!reopened.isDirty());

    const QString html = dir.filePath(QStringLiteral("keep.html"));
    QVERIFY2(doc.save(html, &error), qPrintable(error));
    QFile file(html);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("page-break-before"));
}

void WritePaginationTest::liveEditsRepaginateWithoutAnUndoStep()
{
    WriteDocument doc;
    QTextDocument *text = doc.textDocument();
    doc.keepRules()->setDelay(1);
    const int last = fillLines(doc, 120);
    setKeepWithNext(text, last - 3);
    settle(text);
    doc.keepRules()->applyAll();
    QCOMPARE(KeepRules::count(text), 0); // three lines from the bottom: fine

    // The user types three lines at the top: the heading reaches the bottom.
    QTextCursor cursor(text);
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("One\nTwo\nThree\n"));
    cursor.endEditBlock();
    QTRY_COMPARE(KeepRules::count(text), 1);
    QCOMPARE(pageOf(blockAt(text, last)), 1);

    // One Undo takes back the typing and the break with it; nothing else
    // is left on the stack.
    text->undo();
    QCOMPARE(text->blockCount(), 120);
    QVERIFY(!text->isUndoAvailable());
    QCOMPARE(KeepRules::count(text), 0);
    // A pass must not run while Redo is available (it would discard it).
    QTest::qWait(30);
    QVERIFY(text->isRedoAvailable());
    text->redo();
    QCOMPARE(text->blockCount(), 123);
    QCOMPARE(KeepRules::count(text), 1);
}

void WritePaginationTest::longDocumentStaysQuick()
{
    WriteDocument doc;
    QTextDocument *text = doc.textDocument();
    QTextCursor cursor(text);
    for (int i = 0; i < 3000; ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        cursor.insertText(i % 10 == 0 ? QStringLiteral("Heading %1").arg(i)
                                      : QStringLiteral("Body text that wraps once or twice ").repeated(1 + i % 4));
        if (i % 10 == 0) {
            QTextBlockFormat keep;
            keep.setProperty(Docx::kKeepWithNext, true);
            cursor.mergeBlockFormat(keep);
        }
    }
    settle(text);
    QElapsedTimer clock;
    clock.start();
    doc.keepRules()->applyAll();
    const qint64 ms = clock.elapsed();
    qInfo("3000 paragraphs, %d pages, %d breaks: %lld ms", text->pageCount(), KeepRules::count(text), ms);
    QVERIFY(ms < 20000);
    // Every heading now shares a page with the paragraph after it.
    for (int i = 0; i + 1 < 3000; i += 10) {
        QCOMPARE(pageOf(blockAt(text, i), true), pageOf(blockAt(text, i + 1)));
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WritePaginationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_pagination.moc"
