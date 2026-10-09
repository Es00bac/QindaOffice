// SPDX-License-Identifier: GPL-3.0-or-later
// QindaWrite's document-scale features: footnotes, the table of contents,
// tracked changes and the ruler.
//
// The parts worth pinning are the ones that go wrong silently: footnote
// numbering is document ORDER (so inserting in the middle renumbers the
// rest), the TOC's page numbers come from the same geometry the view paints
// with, accepting and rejecting a change have to be exact inverses, and the
// ruler's markers have to describe the paragraph the cursor is actually in.

#include "write_test_support.h"

#include "qindawrite/src/page_geometry.h"
#include "qindawrite/src/style_catalog.h"
#include "qindawrite/src/write_footnotes.h"
#include "qindawrite/src/write_review_actions.h"
#include "qindawrite/src/write_revisions.h"
#include "qindawrite/src/write_toc.h"

#include "standard_actions.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace ActionId = ::QindaOffice::Quick::ActionId;

class WriteReviewTest : public QObject {
    Q_OBJECT
private slots:
    void footnoteMarkersNumberByDocumentOrder();
    void footnotesLandOnTheirMarkersPage();
    void orphanNotesArePruned();
    void tableOfContentsListsHeadingsWithPages();
    void applyingAStyleRestylesWrittenText();
    void updatingTheTableReplacesItInPlace();
    void trackedInsertionIsMarkedAndRejectable();
    void acceptAndRejectAreExactInverses();
    void rulerMarkersFollowTheCursorParagraph();
    void enterAfterAHeadingStartsBodyText();
    void footnoteDialogRoundTrip();
};

namespace {

QTextDocument *makeDocument(WriteDocument *doc)
{
    return doc->textDocument();
}

int markerCount(const QTextDocument *document)
{
    int count = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().isValid()
                && it.fragment().charFormat().hasProperty(kFootnoteIdProperty)) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

void WriteReviewTest::footnoteMarkersNumberByDocumentOrder()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    QTextCursor cursor(text);
    cursor.insertText(QStringLiteral("alpha"));
    const QString second = insertFootnoteMarker(cursor);
    cursor.insertText(QStringLiteral(" omega"));
    const QString third = insertFootnoteMarker(cursor);
    // Insert a marker BEFORE the first one, which must renumber the rest.
    cursor.setPosition(0);
    const QString first = insertFootnoteMarker(cursor);
    doc.setFootnoteText(first, QStringLiteral("one"));
    doc.setFootnoteText(second, QStringLiteral("two"));
    doc.setFootnoteText(third, QStringLiteral("three"));

    renumberFootnoteMarkers(text);

    PageGeometry geometry;
    const QList<FootnoteRef> refs = collectFootnotes(text, geometry);
    QCOMPARE(refs.size(), 3);
    QCOMPARE(refs.at(0).id, first);
    QCOMPARE(refs.at(0).number, 1);
    QCOMPARE(refs.at(2).id, third);
    QCOMPARE(refs.at(2).number, 3);
    // The visible glyph follows the number, or the page shows "*" forever.
    QVERIFY(text->toPlainText().startsWith(QStringLiteral("1")));
}

void WriteReviewTest::footnotesLandOnTheirMarkersPage()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    QTextCursor cursor(text);
    const QString early = insertFootnoteMarker(cursor);
    // Push a second marker well past the first page.
    for (int i = 0; i < 120; ++i) {
        cursor.insertBlock();
        cursor.insertText(QStringLiteral("filler line %1").arg(i));
    }
    const QString late = insertFootnoteMarker(cursor);
    doc.setFootnoteText(early, QStringLiteral("first note"));
    doc.setFootnoteText(late, QStringLiteral("later note"));

    const PageDecoration decoration = doc.decoration();
    QVERIFY(decoration.footnotes.contains(1));
    QVERIFY(decoration.footnotes.value(1).first().contains(QStringLiteral("first note")));
    // The later marker must not print its note on page one.
    int latePage = 0;
    for (const auto &[page, lines] : decoration.footnotes.asKeyValueRange()) {
        if (lines.join(QString()).contains(QStringLiteral("later note"))) {
            latePage = page;
        }
    }
    QVERIFY2(latePage > 1, qPrintable(QString::number(latePage)));
}

void WriteReviewTest::orphanNotesArePruned()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    QTextCursor cursor(text);
    const QString id = insertFootnoteMarker(cursor);
    doc.setFootnoteText(id, QStringLiteral("note"));
    QHash<QString, QString> notes = doc.footnotes();
    notes.insert(QStringLiteral("ghost"), QStringLiteral("no marker"));

    pruneOrphanFootnotes(text, &notes);

    // A note whose marker is gone would print forever with no way to reach
    // it from the document.
    QVERIFY(notes.contains(id));
    QVERIFY(!notes.contains(QStringLiteral("ghost")));
}

// "Heading 2" on a line already written makes the TEXT a heading, not just
// the paragraph mark (the block char format only reaches later typing).
void WriteReviewTest::applyingAStyleRestylesWrittenText()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    QTextCursor cursor(text);
    cursor.insertText(QStringLiteral("Results"));
    StyleCatalog catalog;
    catalog.apply(QTextCursor(text->begin()), StyleCatalog::Heading2);
    const QTextCharFormat run = text->begin().begin().fragment().charFormat();
    QCOMPARE(run.fontPointSize(), 16.0);
    QCOMPARE(run.fontWeight(), int(QFont::Bold));
    catalog.apply(QTextCursor(text->begin()), StyleCatalog::Normal);
    QCOMPARE(text->begin().begin().fragment().charFormat().fontPointSize(), 11.0);
    QCOMPARE(text->begin().begin().fragment().charFormat().fontWeight(), int(QFont::Normal));
}

void WriteReviewTest::tableOfContentsListsHeadingsWithPages()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    StyleCatalog catalog;
    QTextCursor cursor(text);
    cursor.insertText(QStringLiteral("Introduction"));
    catalog.apply(cursor, StyleCatalog::Heading1);
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    catalog.apply(cursor, StyleCatalog::Normal);
    cursor.insertText(QStringLiteral("Body text"));
    cursor.insertBlock();
    cursor.insertText(QStringLiteral("Details"));
    catalog.apply(cursor, StyleCatalog::Heading2);

    PageGeometry geometry;
    const QList<TocEntry> entries = collectHeadings(text, geometry);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).text, QStringLiteral("Introduction"));
    QCOMPARE(entries.at(0).level, 1);
    QCOMPARE(entries.at(1).level, 2);
    // A Heading 1 at the very start must not force a blank page ahead of
    // itself, or every page number in the document is one out.
    QCOMPARE(entries.at(0).page, 1);

    QTextCursor at(text);
    at.movePosition(QTextCursor::Start);
    QCOMPARE(insertTableOfContents(text, geometry, at), 2);
    QVERIFY(text->toPlainText().contains(QStringLiteral("Contents")));
    // The table must never list itself.
    QCOMPARE(collectHeadings(text, geometry).size(), 2);
}

void WriteReviewTest::updatingTheTableReplacesItInPlace()
{
    WriteDocument doc;
    QTextDocument *text = makeDocument(&doc);
    StyleCatalog catalog;
    QTextCursor cursor(text);
    cursor.insertText(QStringLiteral("One"));
    catalog.apply(cursor, StyleCatalog::Heading1);
    PageGeometry geometry;
    QTextCursor at(text);
    at.movePosition(QTextCursor::End);
    at.insertBlock();
    QCOMPARE(insertTableOfContents(text, geometry, at), 1);

    // Add a heading, then update: the table is REPLACED, not duplicated.
    QTextCursor tail(text);
    tail.movePosition(QTextCursor::End);
    tail.insertBlock();
    tail.insertText(QStringLiteral("Two"));
    catalog.apply(tail, StyleCatalog::Heading1);

    QCOMPARE(updateTableOfContents(text, geometry), 2);
    QCOMPARE(text->toPlainText().count(QStringLiteral("Contents")), 1);
}

void WriteReviewTest::trackedInsertionIsMarkedAndRejectable()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    WriteRevisions *revisions = scene.host.revisions();
    PageTextControl *control = scene.control();
    control->textCursor().insertText(QStringLiteral("original"));
    QCOMPARE(revisions->changeCount(), 0);

    revisions->setRecording(true);
    QTextCursor cursor = control->textCursor();
    cursor.movePosition(QTextCursor::End);
    control->setTextCursor(cursor);
    control->textCursor().insertText(QStringLiteral(" added"));

    QVERIFY(revisions->changeCount() > 0);
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral(" added")));

    revisions->rejectAll();
    // Rejecting an insertion returns the document to what it was.
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("original"));
    QCOMPARE(revisions->changeCount(), 0);
}

void WriteReviewTest::acceptAndRejectAreExactInverses()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    WriteRevisions *revisions = scene.host.revisions();
    PageTextControl *control = scene.control();
    control->textCursor().insertText(QStringLiteral("keep drop"));

    revisions->setRecording(true);
    // Strike "drop" through the same path the Delete key takes.
    QTextCursor cursor = control->textCursor();
    cursor.setPosition(5);
    cursor.setPosition(9, QTextCursor::KeepAnchor);
    control->setTextCursor(cursor);
    scene.page->forceActiveFocus();
    QTRY_VERIFY(scene.page->hasActiveFocus());
    QTest::keyClick(scene.window, Qt::Key_Delete);

    // Struck text stays in the document until the change is resolved.
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("drop")));
    QVERIFY(revisions->changeCount() > 0);

    revisions->acceptAll();
    // Accepting a deletion removes the text; rejecting would have kept it.
    QCOMPARE(scene.doc->textDocument()->toPlainText().trimmed(), QStringLiteral("keep"));
    QCOMPARE(revisions->changeCount(), 0);
}

void WriteReviewTest::rulerMarkersFollowTheCursorParagraph()
{
    Scene scene(QSize(1000, 700));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    QVERIFY(scene.item(QStringLiteral("writeRuler")) != nullptr);
    const qreal margin = scene.doc->pageMargin();
    QCOMPARE(qRound(scene.host.rulerMarkers().value(QStringLiteral("left")).toReal()), qRound(margin));

    PageTextControl *control = scene.control();
    QTextCursor cursor = control->textCursor();
    QTextBlockFormat indented;
    indented.setLeftMargin(48);
    indented.setTextIndent(24);
    cursor.mergeBlockFormat(indented);
    control->setTextCursor(cursor);

    // The markers describe the paragraph the cursor is in, in document px.
    QVariantMap markers = scene.host.rulerMarkers();
    QCOMPARE(qRound(markers.value(QStringLiteral("left")).toReal()), qRound(margin + 48));
    QCOMPARE(qRound(markers.value(QStringLiteral("firstLine")).toReal()), qRound(margin + 48 + 24));
    // Dragging the right marker writes the paragraph's right margin.
    scene.host.applyRulerMarker(QStringLiteral("indent-right"), scene.doc->pageSize().width() - margin - 30);
    QCOMPARE(qRound(control->textCursor().blockFormat().rightMargin()), 30);
    // The Tk.Ruler shows the three markers.
    QQuickItem *ruler = scene.item(QStringLiteral("writeRuler"));
    QTRY_VERIFY(findItem(ruler, QStringLiteral("rulerMarker_indent-left")) != nullptr);
}

void WriteReviewTest::enterAfterAHeadingStartsBodyText()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    PageTextControl *control = scene.control();
    QTextCursor cursor = control->textCursor();
    cursor.insertText(QStringLiteral("Chapter One"));
    StyleCatalog catalog;
    catalog.apply(cursor, StyleCatalog::Heading1);
    cursor.movePosition(QTextCursor::End);
    control->setTextCursor(cursor);
    scene.page->forceActiveFocus();
    QTRY_VERIFY(scene.page->hasActiveFocus());

    QTest::keyClick(scene.window, Qt::Key_Return);

    // Qt carries the block format forward, so without the reset everything
    // after a heading stays a heading — and the table of contents then
    // lists body paragraphs as entries.
    QCOMPARE(classifyBlockStyle(control->textCursor().block()), int(StyleCatalog::Normal));
}

void WriteReviewTest::footnoteDialogRoundTrip()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    scene.control()->insertText(QStringLiteral("claim"));
    QSignalSpy asked(&scene.host, &WriteHost::footnoteRequested);
    scene.host.actions()->trigger(QString::fromLatin1(ActionId::kInsertFootnote));
    QCOMPARE(asked.size(), 1);
    QVERIFY(asked.first().at(0).toString().isEmpty());
    // The dialog's answer inserts the marker and stores the note.
    scene.host.commitFootnote(QString(), QStringLiteral("source"));
    QCOMPARE(markerCount(scene.doc->textDocument()), 1);
    QCOMPARE(scene.doc->footnotes().size(), 1);
    // Double-clicking the marker asks again, with the note's text.
    const int markerPos = scene.doc->textDocument()->toPlainText().size() - 1;
    QVERIFY(scene.host.reviewActions()->editFootnoteAt(markerPos));
    QCOMPARE(asked.size(), 2);
    QCOMPARE(asked.last().at(1).toString(), QStringLiteral("source"));
    // Emptying the note removes marker and note together.
    scene.host.commitFootnote(asked.last().at(0).toString(), QString());
    QCOMPARE(markerCount(scene.doc->textDocument()), 0);
    QVERIFY(scene.doc->footnotes().isEmpty());
}

WRITE_TEST_MAIN(WriteReviewTest)

#include "tst_write_review.moc"
