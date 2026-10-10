// SPDX-License-Identifier: GPL-3.0-or-later
// M84 gates for formatted typing in QindaNote (D-093): every format
// round-trips through the page file, pages written before M84 load
// unchanged, undo is per edit through the real window's editor, lists take
// Tab, tables take Tab and row/column edits, paste keeps formatting and
// nothing else, styles and links apply from the commands.

#include "note_test_support.h"

#include "support/quick_dump.h"

#include "ink_persistence.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_rich_style.h"
#include "qindanote/src/note_rich_text.h"

#include <qindatk/rich_text_area.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_spans.h>

#include <QClipboard>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;
using namespace QindaTK::RichText;

namespace {

const Paragraph *paragraphAt(const QindaTK::RichTextDocument &doc, int index)
{
    return doc.paragraphsInOrder().value(index);
}

CharFormat formatAt(const QindaTK::RichTextDocument &doc, int paragraph, int offset)
{
    const Paragraph *p = paragraphAt(doc, paragraph);
    return doc.charFormat(Spans::formatAt(p->spans, offset));
}

Position at(const QindaTK::RichTextDocument &doc, int paragraph, int offset)
{
    return {paragraphAt(doc, paragraph)->id, offset};
}

void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1());
    }
}

const Table *firstTable(const QindaTK::RichTextDocument &doc)
{
    for (const Block &block : doc.blocks()) {
        if (block.table) {
            return block.table.get();
        }
    }
    return nullptr;
}

} // namespace

class NoteRichTest final : public QObject {
    Q_OBJECT

private slots:
    void everyFormatRoundTripsThroughThePageFile()
    {
        auto doc = RichBlock::fromPlainText(QStringLiteral(
            "Bold Italic Under Strike Super Sub Font Size Red Mark Link\nA title\nA heading\nA quote\n"
            "some_code()\nBullet\nNumber"));
        const QString first = paragraphAt(*doc, 0)->text;
        const auto word = [&](const char *w) { return int(first.indexOf(QLatin1String(w))); };
        const auto format = [&](const char *w, CharFormatPatch patch) {
            const int from = word(w);
            QVERIFY(doc->applyCharFormat(at(*doc, 0, from), at(*doc, 0, from + int(qstrlen(w))), patch));
        };
        CharFormatPatch patch;
        patch.weight = 700;
        format("Bold", patch);
        patch = {};
        patch.italic = 1;
        format("Italic", patch);
        patch = {};
        patch.underline = 1;
        format("Under", patch);
        patch = {};
        patch.strike = 1;
        format("Strike", patch);
        patch = {};
        patch.verticalAlign = VerticalAlign::Superscript;
        format("Super", patch);
        patch = {};
        patch.verticalAlign = VerticalAlign::Subscript;
        format("Sub", patch);
        patch = {};
        patch.family = QStringLiteral("DejaVu Serif");
        format("Font", patch);
        patch = {};
        patch.sizePt = 18;
        format("Size", patch);
        patch = {};
        patch.color = QColor(0xc0, 0x1c, 0x28);
        format("Red", patch);
        patch = {};
        patch.background = QColor(0xff, 0xe1, 0x4d);
        format("Mark", patch);
        patch = {};
        patch.href = QStringLiteral("https://example.org/");
        format("Link", patch);
        QVERIFY(RichStyle::apply(*doc, {at(*doc, 1, 0), at(*doc, 1, 0)}, RichStyle::Style::Title));
        QVERIFY(RichStyle::apply(*doc, {at(*doc, 2, 0), at(*doc, 2, 0)}, RichStyle::Style::Heading2));
        QVERIFY(RichStyle::apply(*doc, {at(*doc, 3, 0), at(*doc, 3, 0)}, RichStyle::Style::Quote));
        QVERIFY(RichStyle::apply(*doc, {at(*doc, 4, 0), at(*doc, 4, 0)}, RichStyle::Style::Code));
        QVERIFY(doc->setList(at(*doc, 5, 0), at(*doc, 5, 0), ListKind::Disc));
        QVERIFY(doc->setList(at(*doc, 6, 0), at(*doc, 6, 0), ListKind::Decimal));
        QVERIFY(doc->insertTable(at(*doc, 6, 6), 2, 2, 1).has_value());
        const Table *table = firstTable(*doc);
        QVERIFY(table != nullptr);
        const QString tableId = table->id.toString(QUuid::WithoutBraces);
        QVERIFY(doc->setTableCellText(tableId, table->rows.at(0).cells.at(0).id.toString(QUuid::WithoutBraces),
                                      QStringLiteral("Head")));
        QVERIFY(doc->setTableCellText(tableId, table->rows.at(1).cells.at(1).id.toString(QUuid::WithoutBraces),
                                      QStringLiteral("Cell")));

        QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")}};
        RichBlock::store(&block, *doc);
        QVERIFY(block.contains(QStringLiteral("rich")));
        QVERIFY(block.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Bold Italic")));

        // Through the notebook's files and back.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Rich"));
        Notebook notebook;
        NoteSection &section = notebook.addSection(QStringLiteral("S"));
        NotePage &page = notebook.addPage(section, QStringLiteral("P"));
        page.envelope.blocksJson = withBlocksArray(QByteArray("{}"), QJsonArray{block});
        const QString sectionId = section.id;
        const QString pageId = page.id;
        QString error;
        QVERIFY2(notebook.create(path, &error), qPrintable(error));
        QFile file(path + QStringLiteral("/sections/%1/pages/%2.json").arg(sectionId, pageId));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("\"version\":2"));
        Notebook reopened;
        QVERIFY(reopened.load(path, &error));
        NotePage *loaded = reopened.pageById(sectionId, pageId);
        QVERIFY(reopened.loadPage(*reopened.sectionById(sectionId), *loaded, &error));
        QCOMPARE(pageFormatVersion(loaded->envelope.blocksJson), 2);
        const QJsonObject back = blocksArray(loaded->envelope.blocksJson).first().toObject();
        const auto read = RichBlock::forBlock(back);
        QCOMPARE(RichBlock::plainText(*read), RichBlock::plainText(*doc));
        QCOMPARE(formatAt(*read, 0, word("Bold")).weight, 700);
        QCOMPARE(formatAt(*read, 0, word("Italic")).italic, qint8(1));
        QCOMPARE(formatAt(*read, 0, word("Under")).underline, qint8(1));
        QCOMPARE(formatAt(*read, 0, word("Strike")).strike, qint8(1));
        QCOMPARE(formatAt(*read, 0, word("Super")).verticalAlign, VerticalAlign::Superscript);
        QCOMPARE(formatAt(*read, 0, word("Sub")).verticalAlign, VerticalAlign::Subscript);
        QCOMPARE(formatAt(*read, 0, word("Font")).family, QStringLiteral("DejaVu Serif"));
        QCOMPARE(formatAt(*read, 0, word("Size")).sizePt, 18.0);
        QCOMPARE(formatAt(*read, 0, word("Red")).color, QColor(0xc0, 0x1c, 0x28));
        QCOMPARE(formatAt(*read, 0, word("Mark")).background, QColor(0xff, 0xe1, 0x4d));
        QCOMPARE(formatAt(*read, 0, word("Link")).href, QStringLiteral("https://example.org/"));
        QCOMPARE(RichStyle::at(*read, at(*read, 1, 0)), RichStyle::Style::Title);
        QCOMPARE(RichStyle::at(*read, at(*read, 2, 0)), RichStyle::Style::Heading2);
        QCOMPARE(RichStyle::at(*read, at(*read, 3, 0)), RichStyle::Style::Quote);
        QCOMPARE(RichStyle::at(*read, at(*read, 4, 0)), RichStyle::Style::Code);
        QVERIFY(!paragraphAt(*read, 5)->format.listId.isEmpty());
        QVERIFY(!paragraphAt(*read, 6)->format.listId.isEmpty());
        QVERIFY(paragraphAt(*read, 5)->format.listId != paragraphAt(*read, 6)->format.listId);
        const Table *readTable = firstTable(*read);
        QVERIFY(readTable != nullptr);
        QCOMPARE(readTable->rows.size(), 2);
        QCOMPARE(readTable->headerRows, 1);
        QCOMPARE(readTable->rows.at(1).cells.at(1).paragraphs.first()->text, QStringLiteral("Cell"));
        // The page lays it out as formatted text (the painter's path).
        const QList<PageBlock> laid = layoutBlocks(loaded->envelope.blocksJson, 760);
        QCOMPARE(laid.size(), 1);
        QVERIFY(!laid.first().rich.isEmpty());
        QVERIFY(laid.first().rect.height() > 200);
    }


    void pagesFromBeforeM84LoadUnchanged()
    {
        // A page as M65 wrote it: no version, plain text, a checklist, notes.
        const QJsonArray blocks{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Plain old text\nsecond line")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("check")}, {QStringLiteral("text"), QStringLiteral("Buy milk")},
                        {QStringLiteral("checked"), true}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")}, {QStringLiteral("text"), QStringLiteral("## Notes\n- **one**")}},
        };
        QindaOffice::Ink::PageEnvelope envelope;
        envelope.id = QStringLiteral("p1");
        envelope.title = QStringLiteral("Old page");
        envelope.background = QStringLiteral("ruled");
        envelope.blocksJson = QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}}).toJson(QJsonDocument::Compact);
        const QByteArray bytes = QindaOffice::Ink::pageToJson(envelope);
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sections/s1/pages")));
        QFile index(dir.filePath(QStringLiteral("notebook.json")));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write(R"({"name":"Old","sections":[{"id":"s1","name":"Notes","pages":[{"id":"p1","title":"Old page"}]}]})");
        index.close();
        QFile pageFile(dir.filePath(QStringLiteral("sections/s1/pages/p1.json")));
        QVERIFY(pageFile.open(QIODevice::WriteOnly));
        pageFile.write(bytes);
        pageFile.close();

        Notebook notebook;
        QString error;
        QVERIFY(notebook.load(dir.path(), &error));
        NotePage *page = notebook.pageById(QStringLiteral("s1"), QStringLiteral("p1"));
        QVERIFY(notebook.loadPage(*notebook.sectionById(QStringLiteral("s1")), *page, &error));
        QCOMPARE(pageFormatVersion(page->envelope.blocksJson), 1);
        const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, 760);
        QCOMPARE(laid.size(), 3);
        QCOMPARE(laid.at(0).kind, PageBlock::Kind::Text);
        QCOMPARE(laid.at(0).text, QStringLiteral("Plain old text\nsecond line"));
        QVERIFY(laid.at(0).rich.isEmpty()); // painted exactly as before M84
        QCOMPARE(laid.at(0).rect.height(), textBlockHeight(laid.at(0).text, laid.at(0).rect.width()));
        QCOMPARE(laid.at(1).kind, PageBlock::Kind::Check);
        QVERIFY(laid.at(1).checked);
        QCOMPARE(laid.at(2).kind, PageBlock::Kind::Markdown);
        // Saved without an edit, the page file is byte for byte what it was.
        QVERIFY(notebook.save(&error));
        QVERIFY(pageFile.open(QIODevice::ReadOnly));
        QCOMPARE(pageFile.readAll(), bytes);
        pageFile.close();
        // Opening the block and closing it without typing keeps it plain.
        NotePageItem item;
        item.setPage(page);
        item.beginEdit(0);
        QVERIFY(item.richDocument() != nullptr);
        QCOMPARE(item.editingText(), QStringLiteral("Plain old text\nsecond line"));
        item.commitEdit();
        QVERIFY(!blocksArray(page->envelope.blocksJson).first().toObject().contains(QStringLiteral("rich")));
        // An older QindaNote that rewrote `text` under a stale `rich`: the
        // text wins.
        auto doc = RichBlock::fromPlainText(QStringLiteral("formatted"));
        QJsonObject stale{{QStringLiteral("type"), QStringLiteral("text")}};
        RichBlock::store(&stale, *doc);
        stale.insert(QStringLiteral("rich"), RichBlock::toJson(*doc));
        stale.insert(QStringLiteral("text"), QStringLiteral("edited elsewhere"));
        const QList<PageBlock> staleLaid =
            layoutBlocks(withBlocksArray(QByteArray("{}"), QJsonArray{stale}), 760);
        QVERIFY(staleLaid.first().rich.isEmpty());
        QCOMPARE(RichBlock::plainText(*RichBlock::forBlock(stale)), QStringLiteral("edited elsewhere"));
    }

    void undoIsOneStepPerEdit()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.page->addTextBlock();
        QTRY_VERIFY(scene.page->richArea() != nullptr && scene.page->richArea()->hasActiveFocus());
        typeText(scene.window, QStringLiteral("Hello"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("Hello"));
        scene.page->richArea()->selectAll();
        scene.host.actions()->trigger(QStringLiteral("formatBoldAction"));
        QTRY_COMPARE(formatAt(*scene.page->richDocument(), 0, 1).weight, 700);
        QVERIFY(scene.host.actions()->entry(QStringLiteral("formatBoldAction"))->isChecked());
        QTest::keyClick(scene.window, Qt::Key_End);
        typeText(scene.window, QStringLiteral(" world"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("Hello world"));
        // One step each: the typing run, the bold, the first typing run.
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("Hello"));
        QVERIFY(scene.page->editingBlock() >= 0); // the editor stays open
        QCOMPARE(formatAt(*scene.page->richDocument(), 0, 1).weight, 700);
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QTRY_VERIFY(formatAt(*scene.page->richDocument(), 0, 1).weight != 700);
        QCOMPARE(scene.page->editingText(), QStringLiteral("Hello"));
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QTRY_COMPARE(scene.page->editingText(), QString());
        scene.host.actions()->trigger(QStringLiteral("editRedoAction"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("Hello"));
        scene.host.actions()->trigger(QStringLiteral("editRedoAction"));
        QTRY_COMPARE(formatAt(*scene.page->richDocument(), 0, 1).weight, 700);
        // The page file holds what the editor shows.
        const QJsonObject stored = blocksArray(scene.page->page()->envelope.blocksJson).first().toObject();
        QCOMPARE(stored.value(QStringLiteral("text")).toString(), QStringLiteral("Hello"));
        QVERIFY(stored.contains(QStringLiteral("rich")));
    }

    void listsTakeTabAndTablesTakeTabAndEdits()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.page->addTextBlock();
        QTRY_VERIFY(scene.page->richArea() != nullptr && scene.page->richArea()->hasActiveFocus());
        scene.host.actions()->trigger(QStringLiteral("formatListBulletAction"));
        typeText(scene.window, QStringLiteral("one"));
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTest::keyClick(scene.window, Qt::Key_Tab);
        typeText(scene.window, QStringLiteral("two"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("one\ntwo"));
        const QindaTK::RichTextDocument *doc = scene.page->richDocument();
        QVERIFY(!paragraphAt(*doc, 0)->format.listId.isEmpty());
        QCOMPARE(paragraphAt(*doc, 1)->format.listLevel, 1);
        QVERIFY(scene.host.actions()->entry(QStringLiteral("formatListBulletAction"))->isChecked());
        // A table in the same block: Tab goes from cell to cell.
        QTest::keyClick(scene.window, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTest::keyClick(scene.window, Qt::Key_Return); // out of the list
        scene.host.insertTable(2, 2);
        QTRY_VERIFY(scene.host.ribbonContexts().contains(QStringLiteral("noteTable")));
        typeText(scene.window, QStringLiteral("a"));
        QTest::keyClick(scene.window, Qt::Key_Tab);
        typeText(scene.window, QStringLiteral("b"));
        const Table *table = firstTable(*scene.page->richDocument());
        QVERIFY(table != nullptr);
        QTRY_COMPARE(table->rows.at(0).cells.at(1).paragraphs.first()->text, QStringLiteral("b"));
        QCOMPARE(table->rows.at(0).cells.at(0).paragraphs.first()->text, QStringLiteral("a"));
        scene.host.actions()->trigger(QStringLiteral("tableInsertRowBelowAction"));
        scene.host.actions()->trigger(QStringLiteral("tableInsertColumnRightAction"));
        table = firstTable(*scene.page->richDocument());
        QCOMPARE(table->rows.size(), 3);
        QCOMPARE(table->columnWeights.size(), 3);
        scene.host.actions()->trigger(QStringLiteral("tableDeleteRowAction"));
        scene.host.actions()->trigger(QStringLiteral("tableDeleteColumnAction"));
        table = firstTable(*scene.page->richDocument());
        QCOMPARE(table->rows.size(), 2);
        QCOMPARE(table->columnWeights.size(), 2);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_table"));
        scene.host.actions()->trigger(QStringLiteral("noteTableDeleteAction"));
        QTRY_VERIFY(firstTable(*scene.page->richDocument()) == nullptr);
        QVERIFY(!scene.host.ribbonContexts().contains(QStringLiteral("noteTable")));
    }

    void pasteKeepsFormattingAndNothingElse()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.page->addTextBlock();
        QTRY_VERIFY(scene.page->richArea() != nullptr && scene.page->richArea()->hasActiveFocus());
        auto *mime = new QMimeData;
        mime->setHtml(QStringLiteral(
            "<html><head><style>p{color:red}</style><script>alert('x')</script></head><body>"
            "<p onclick=\"steal()\"><b>Bold</b> and <span style=\"color:#c01c28\">red</span> "
            "<a href=\"javascript:steal()\">bad</a> <a href=\"https://example.org\">good</a>"
            "<img src=\"https://tracker.example/pixel.png\" alt=\"pixel\"></p></body></html>"));
        mime->setText(QStringLiteral("Bold and red bad good"));
        QGuiApplication::clipboard()->setMimeData(mime);
        QTest::keyClick(scene.window, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY(scene.page->editingText().contains(QStringLiteral("Bold")));
        const QindaTK::RichTextDocument *doc = scene.page->richDocument();
        const QString text = scene.page->editingText();
        QVERIFY2(!text.contains(QStringLiteral("alert")), qPrintable(text));
        QVERIFY(!text.contains(QStringLiteral("color:red")));
        QVERIFY(!paragraphAt(*doc, 0)->text.contains(QChar(ObjectChar))); // no remote picture
        const Paragraph *p = paragraphAt(*doc, 0);
        QCOMPARE(formatAt(*doc, 0, int(p->text.indexOf(QStringLiteral("Bold")))).weight, 700);
        QCOMPARE(formatAt(*doc, 0, int(p->text.indexOf(QStringLiteral("red")))).color, QColor(0xc0, 0x1c, 0x28));
        QVERIFY(formatAt(*doc, 0, int(p->text.indexOf(QStringLiteral("bad")))).href.isEmpty());
        QCOMPARE(formatAt(*doc, 0, int(p->text.indexOf(QStringLiteral("good")))).href,
                 QStringLiteral("https://example.org"));
    }

    void stylesAndLinksApplyFromTheCommands()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.page->addTextBlock();
        QTRY_VERIFY(scene.page->richArea() != nullptr && scene.page->richArea()->hasActiveFocus());
        typeText(scene.window, QStringLiteral("Plan"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("Plan"));
        scene.host.actions()->trigger(QStringLiteral("noteStyle_title"));
        QTRY_COMPARE(scene.page->richStyleAtCaret(), int(RichStyle::Style::Title));
        QVERIFY(scene.host.actions()->entry(QStringLiteral("noteStyle_title"))->isChecked());
        scene.host.actions()->trigger(QStringLiteral("noteStyle_heading2"));
        QTRY_COMPARE(paragraphAt(*scene.page->richDocument(), 0)->format.headingLevel, 2);
        // One undo step takes the heading off again (the Title comes back).
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QTRY_COMPARE(scene.page->richStyleAtCaret(), int(RichStyle::Style::Title));
        scene.host.actions()->trigger(QStringLiteral("noteStyle_normal"));
        QTRY_COMPARE(scene.page->richStyleAtCaret(), int(RichStyle::Style::Normal));
        QTest::keyClick(scene.window, Qt::Key_End);
        scene.host.applyLink(QStringLiteral("example.org"), QStringLiteral(" site"));
        const Paragraph *p = paragraphAt(*scene.page->richDocument(), 0);
        QTRY_VERIFY(p->text.endsWith(QStringLiteral(" site")));
        QCOMPARE(formatAt(*scene.page->richDocument(), 0, int(p->text.size()) - 1).href,
                 QStringLiteral("https://example.org"));
        // Linked notes: a link to a document, named after the file.
        scene.host.linkFile(QUrl::fromLocalFile(QStringLiteral("/tmp/Quarterly report.docx")));
        p = paragraphAt(*scene.page->richDocument(), 0);
        QTRY_VERIFY(p->text.endsWith(QStringLiteral("Quarterly report.docx")));
        QCOMPARE(formatAt(*scene.page->richDocument(), 0, int(p->text.size()) - 1).href,
                 QUrl::fromLocalFile(QStringLiteral("/tmp/Quarterly report.docx")).toString());
        // Ctrl+K asks the window for the link dialog.
        QSignalSpy asked(&scene.host, &NoteHost::linkDialogRequested);
        QTest::keyClick(scene.window, Qt::Key_K, Qt::ControlModifier);
        QTRY_COMPARE(asked.count(), 1);
    }
};

NOTE_TEST_MAIN(NoteRichTest)

#include "tst_note_rich.moc"
