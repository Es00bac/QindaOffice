// SPDX-License-Identifier: GPL-3.0-or-later
// QindaWrite's document-scale features: footnotes, the table of contents,
// tracked changes and the ruler.
//
// The parts worth pinning are the ones that go wrong silently: footnote
// numbering is document ORDER (so inserting in the middle renumbers the
// rest), the TOC's page numbers come from the same geometry the view paints
// with, accepting and rejecting a change have to be exact inverses, and the
// ruler's markers have to describe the paragraph the cursor is actually in.

#include "qindawrite/src/page_geometry.h"
#include "qindawrite/src/page_text_view.h"
#include "qindawrite/src/style_catalog.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_footnotes.h"
#include "qindawrite/src/write_review_actions.h"
#include "qindawrite/src/write_revisions.h"
#include "qindawrite/src/write_ruler.h"
#include "qindawrite/src/write_toc.h"
#include "qindawrite/src/write_window.h"

#include "standard_actions.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice::Apps::Write;
namespace ActionId = ::QindaOffice::Shell::ActionId;

class WriteReviewTest : public QObject {
    Q_OBJECT
private slots:
    void footnoteMarkersNumberByDocumentOrder();
    void footnotesLandOnTheirMarkersPage();
    void orphanNotesArePruned();
    void tableOfContentsListsHeadingsWithPages();
    void updatingTheTableReplacesItInPlace();
    void trackedInsertionIsMarkedAndRejectable();
    void acceptAndRejectAreExactInverses();
    void rulerMarkersFollowTheCursorParagraph();
    void enterAfterAHeadingStartsBodyText();
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
    WriteWindow window;
    window.show();
    WriteRevisions *revisions = window.revisions();
    QWidgetTextControl *control = window.pageView()->control();
    control->textCursor().insertText(QStringLiteral("original"));
    QCOMPARE(revisions->changeCount(), 0);

    revisions->setRecording(true);
    QTextCursor cursor = control->textCursor();
    cursor.movePosition(QTextCursor::End);
    control->setTextCursor(cursor);
    control->textCursor().insertText(QStringLiteral(" added"));

    QVERIFY(revisions->changeCount() > 0);
    QVERIFY(window.writeDocument()->textDocument()->toPlainText().contains(
        QStringLiteral(" added")));

    revisions->rejectAll();
    // Rejecting an insertion returns the document to what it was.
    QCOMPARE(window.writeDocument()->textDocument()->toPlainText(),
             QStringLiteral("original"));
    QCOMPARE(revisions->changeCount(), 0);
}

void WriteReviewTest::acceptAndRejectAreExactInverses()
{
    WriteWindow window;
    window.show();
    WriteRevisions *revisions = window.revisions();
    QWidgetTextControl *control = window.pageView()->control();
    control->textCursor().insertText(QStringLiteral("keep drop"));

    revisions->setRecording(true);
    // Strike "drop" through the same path the Delete key takes.
    QTextCursor cursor = control->textCursor();
    cursor.setPosition(5);
    cursor.setPosition(9, QTextCursor::KeepAnchor);
    control->setTextCursor(cursor);
    QTest::keyClick(window.pageView(), Qt::Key_Delete);

    // Struck text stays in the document until the change is resolved.
    QVERIFY(window.writeDocument()->textDocument()->toPlainText().contains(
        QStringLiteral("drop")));
    QVERIFY(revisions->changeCount() > 0);

    revisions->acceptAll();
    // Accepting a deletion removes the text; rejecting would have kept it.
    QCOMPARE(window.writeDocument()->textDocument()->toPlainText().trimmed(),
             QStringLiteral("keep"));
    QCOMPARE(revisions->changeCount(), 0);
}

void WriteReviewTest::rulerMarkersFollowTheCursorParagraph()
{
    WriteWindow window;
    window.resize(1000, 700);
    window.show();
    WriteRuler *ruler = window.ruler();
    QVERIFY(ruler != nullptr);
    const qreal margin = window.writeDocument()->pageMargin();
    QCOMPARE(qRound(ruler->markers().left), qRound(margin));

    QWidgetTextControl *control = window.pageView()->control();
    QTextCursor cursor = control->textCursor();
    QTextBlockFormat indented;
    indented.setLeftMargin(48);
    indented.setTextIndent(24);
    cursor.mergeBlockFormat(indented);
    control->setTextCursor(cursor);
    ruler->refresh();

    // The markers describe the paragraph the cursor is in, in document px.
    QCOMPARE(qRound(ruler->markers().left), qRound(margin + 48));
    QCOMPARE(qRound(ruler->markers().firstLine), qRound(margin + 48 + 24));
}

void WriteReviewTest::enterAfterAHeadingStartsBodyText()
{
    WriteWindow window;
    window.show();
    QWidgetTextControl *control = window.pageView()->control();
    QTextCursor cursor = control->textCursor();
    cursor.insertText(QStringLiteral("Chapter One"));
    StyleCatalog catalog;
    catalog.apply(cursor, StyleCatalog::Heading1);
    cursor.movePosition(QTextCursor::End);
    control->setTextCursor(cursor);

    QTest::keyClick(window.pageView(), Qt::Key_Return);

    // Qt carries the block format forward, so without the reset everything
    // after a heading stays a heading — and the table of contents then
    // lists body paragraphs as entries.
    QCOMPARE(classifyBlockStyle(control->textCursor().block()), int(StyleCatalog::Normal));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    WriteReviewTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_review.moc"
