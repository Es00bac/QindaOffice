// SPDX-License-Identifier: GPL-3.0-or-later
// M84 gates for tags (D-093): toggled on paragraphs and blocks with undo,
// To Do boxes tick and carry to the next line, Find Tags lists them across
// the page, section and notebook and goes to them, custom tags keep their
// look, the summary page collects them — through the real window where the
// window is the subject.

#include "note_test_support.h"

#include "support/quick_dump.h"

#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_rich_text.h"
#include "qindanote/src/note_tag_summary.h"
#include "qindanote/src/note_tags.h"

#include <qindatk/rich_text_area.h>
#include <qindatk/rich_text_document.h>

#include <QJsonArray>
#include <QJsonObject>

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;

namespace {

QJsonArray tagsOf(const NotePage *page, int block)
{
    return blocksArray(page->envelope.blocksJson).at(block).toObject().value(QStringLiteral("tags")).toArray();
}

void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1());
    }
}

} // namespace

class NoteTagsTest final : public QObject {
    Q_OBJECT

private slots:
    void tagsToggleOnParagraphsAndUndo()
    {
        NotePageItem item;
        NotePage page;
        page.loaded = true;
        item.setPage(&page);
        item.addTextBlock();
        item.updateEdit(QStringLiteral("first\nsecond"));
        const Tags::TagDef important = Tags::byId(QStringLiteral("important"));
        QVERIFY(item.toggleTag(important));
        // No editor on screen: every paragraph of the open block.
        QCOMPARE(tagsOf(&page, 0).size(), 2);
        QVERIFY(blocksArray(page.envelope.blocksJson).first().toObject().contains(QStringLiteral("rich")));
        QVERIFY(item.editingBlock() >= 0); // tagging keeps the editor open
        QVERIFY(item.toggleTag(important));
        QVERIFY(tagsOf(&page, 0).isEmpty());
        item.undo();
        QCOMPARE(tagsOf(&page, 0).size(), 2);
        QVERIFY(item.editingBlock() >= 0);
        item.redo();
        QVERIFY(tagsOf(&page, 0).isEmpty());
        // A tag with nothing open starts a new tagged note (OneNote).
        item.commitEdit();
        const int blocks = int(blocksArray(page.envelope.blocksJson).size());
        QVERIFY(item.toggleTag(Tags::byId(QStringLiteral("todo"))));
        QCOMPARE(blocksArray(page.envelope.blocksJson).size(), blocks + 1);
        QCOMPARE(tagsOf(&page, blocks).size(), 1);
        item.commitEdit(); // an empty To Do line is content, not litter
        QCOMPARE(blocksArray(page.envelope.blocksJson).size(), blocks + 1);
        // A checklist block takes a whole-block tag.
        item.addCheckItem();
        item.updateEdit(QStringLiteral("call back"));
        QVERIFY(item.toggleTag(Tags::byId(QStringLiteral("phone"))));
        const QJsonArray checkTags = tagsOf(&page, blocks + 1);
        QCOMPARE(checkTags.size(), 1);
        QVERIFY(!checkTags.first().toObject().contains(QStringLiteral("p")));
    }

    void todoBoxesTickAndCarryToTheNextLine()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.page->addTextBlock();
        QTRY_VERIFY(scene.page->richArea() != nullptr && scene.page->richArea()->hasActiveFocus());
        typeText(scene.window, QStringLiteral("milk"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("milk"));
        QTest::keyClick(scene.window, Qt::Key_1, Qt::ControlModifier); // To Do (Ctrl+1)
        QTRY_COMPARE(tagsOf(scene.page->page(), 0).size(), 1);
        QTest::keyClick(scene.window, Qt::Key_Return);
        typeText(scene.window, QStringLiteral("eggs"));
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("milk\neggs"));
        QTRY_COMPARE(tagsOf(scene.page->page(), 0).size(), 2); // the next line is a To Do too
        QTest::keyClick(scene.window, Qt::Key_2, Qt::ControlModifier); // Important on "eggs"
        QTRY_COMPARE(tagsOf(scene.page->page(), 0).size(), 3);
        // The box sits in the margin left of the first line.
        const QList<Tags::Placed> placed = Tags::place(scene.page->blocks().first());
        QVERIFY(!placed.isEmpty());
        QVERIFY(placed.first().def.todo);
        QVERIFY(placed.first().rect.right() <= scene.page->blocks().first().rect.left());
        QVERIFY(scene.page->toggleTodoBoxAt(placed.first().rect.center()));
        QVERIFY(tagsOf(scene.page->page(), 0).at(placed.first().entry).toObject().value(QStringLiteral("done")).toBool());
        QVERIFY(scene.page->editingBlock() >= 0); // ticking does not close the editor
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_tags"));
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QTRY_VERIFY(!tagsOf(scene.page->page(), 0).at(placed.first().entry).toObject().value(QStringLiteral("done")).toBool());
    }

    void findTagsListsAndGoesToThem()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // Page 1: an Important line; page 2 (same section): two To Do lines;
        // a second section: a Question.
        // The editor's selection is what a tag applies to: all of it here.
        const auto tagAll = [&scene](const char *id) {
            QTRY_VERIFY(scene.page->richArea() != nullptr);
            scene.page->richArea()->selectAll();
            scene.page->toggleTag(Tags::byId(QString::fromLatin1(id)));
        };
        scene.page->addTextBlock();
        scene.page->updateEdit(QStringLiteral("Budget is due"));
        tagAll("important");
        scene.page->commitEdit();
        const QString firstPage = scene.host.currentPageId();
        scene.host.actions()->trigger(QStringLiteral("notePageNew"));
        scene.page->addTextBlock();
        scene.page->updateEdit(QStringLiteral("Call Ana\nBook the room"));
        tagAll("todo");
        scene.page->commitEdit();
        scene.host.createSection(QStringLiteral("Other"));
        scene.page->addTextBlock();
        scene.page->updateEdit(QStringLiteral("Why the delay?"));
        tagAll("question");
        scene.page->commitEdit();
        NoteTagSummary *summary = scene.host.tagSummary();
        summary->setScope(QStringLiteral("notebook"));
        summary->refresh();
        QCOMPARE(summary->itemCount(), 4);
        // By tag, in OneNote's order: To Do (2), Important, Question.
        QCOMPARE(summary->data(summary->index(0), NoteTagSummary::TextRole).toString(), QStringLiteral("To Do"));
        QCOMPARE(summary->data(summary->index(1), NoteTagSummary::TextRole).toString(), QStringLiteral("Call Ana"));
        QCOMPARE(summary->data(summary->index(3), NoteTagSummary::TextRole).toString(), QStringLiteral("Important"));
        summary->setScope(QStringLiteral("section")); // "Other" is open
        QCOMPARE(summary->itemCount(), 1);
        summary->setScope(QStringLiteral("notebook"));
        // The pane shows them; a click goes to the page and the line.
        scene.host.actions()->trigger(QStringLiteral("noteFindTagsAction"));
        QQuickItem *panel = nullptr;
        QTRY_VERIFY((panel = findItem(scene.window->contentItem(), QStringLiteral("tagsPanel"))) != nullptr
                    && panel->isVisible());
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_find_tags"));
        summary->activate(4); // Important → "Budget is due" on the first page
        QTRY_COMPARE(scene.host.currentPageId(), firstPage);
        QTRY_VERIFY(scene.page->editingBlock() == 0);
        // A To Do ticked from the pane lands on its page.
        summary->setDone(1, true);
        QVERIFY(summary->data(summary->index(1), NoteTagSummary::DoneRole).toBool());
        // The summary page collects them, tags and all.
        scene.host.actions()->trigger(QStringLiteral("noteTagSummaryPageAction"));
        QTRY_COMPARE(scene.host.currentPageTitle(), QStringLiteral("Tag Summary"));
        const QJsonObject block = blocksArray(scene.host.currentPage()->envelope.blocksJson).first().toObject();
        QCOMPARE(block.value(QStringLiteral("tags")).toArray().size(), 4);
        QVERIFY(block.value(QStringLiteral("text")).toString().contains(QStringLiteral("Book the room")));
    }

    void customTagsKeepTheirLook()
    {
        NoteHost host;
        QVERIFY(host.addCustomTag(QStringLiteral("Waiting for"), QStringLiteral("clock"), QColor(0x26, 0xa2, 0x69)));
        QVariantMap custom;
        for (const QVariant &entry : host.tagCatalog()) {
            if (entry.toMap().value(QStringLiteral("custom")).toBool()) {
                custom = entry.toMap();
            }
        }
        QCOMPARE(custom.value(QStringLiteral("name")).toString(), QStringLiteral("Waiting for"));
        const Tags::TagDef def = Tags::byId(custom.value(QStringLiteral("id")).toString());
        QVERIFY(def.isValid());
        const QJsonObject entry = Tags::entryFor(def, QStringLiteral("p1"));
        QCOMPARE(entry.value(QStringLiteral("name")).toString(), QStringLiteral("Waiting for"));
        QCOMPARE(entry.value(QStringLiteral("icon")).toString(), QStringLiteral("clock"));
        // Removed from the user's list, a note that has it still shows it.
        host.removeCustomTag(def.id);
        QVERIFY(!Tags::byId(def.id).isValid());
        QCOMPARE(Tags::fromEntry(entry).name, QStringLiteral("Waiting for"));
        QCOMPARE(Tags::fromEntry(entry).color, QColor(0x26, 0xa2, 0x69));
    }
};

NOTE_TEST_MAIN(NoteTagsTest)

#include "tst_note_tags.moc"
