// SPDX-License-Identifier: GPL-3.0-or-later
// QindaWrite through the real window: named strips and menus with
// persistent entries; character and paragraph formatting round-trips
// between the entries, the document and the synced check marks;
// find/replace with wrap; the edit commands follow the control; the page
// renders as paper under any theme; zoom is the viewport's; the strip
// wraps instead of hiding; page setup round-trips; structural fixtures.

#include <QRegularExpression>
#include "write_test_support.h"

#include "qindawrite/src/format_controller.h"
#include "qindawrite/src/page_setup_io.h"
#include "qindawrite/src/table_actions.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include "menu_tree.h"
#include "standard_actions.h"

#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextList>
#include <QTextTable>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::MenuNode;
namespace ActionId = QindaOffice::Quick::ActionId;

namespace {

bool menuContains(const MenuNode *node, const QString &id)
{
    if (node == nullptr) {
        return false;
    }
    for (const MenuNode &child : node->children) {
        if (child.id == id || menuContains(&child, id)) {
            return true;
        }
    }
    return false;
}

QString sid(const char *id)
{
    return QString::fromLatin1(id);
}

} // namespace

class WriteUiTest : public QObject {
    Q_OBJECT
private slots:
    // Every case fails on a QML binding loop: the dock panel toggles used to
    // bounce between the View action and DockHost.hiddenPanels.
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    // D-041: the ribbon draws the host's own entries — the named buttons
    // exist, bind live, and the font widgets ride in the Font group.
    void ribbonExposesNamedCommands()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(100);
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        QCOMPARE(ribbon->property("currentTabId").toString(), QStringLiteral("home"));
        const WriteActions &format = scene.host.formatController()->actions();
        for (ActionEntry *entry : format.all()) {
            QVERIFY2(!entry->id().isEmpty(), qPrintable(entry->text()));
        }
        for (ActionEntry *entry : {format.bold, format.italic, format.underline, format.alignLeft,
                                   format.listBullet}) {
            QVERIFY2(findItem(ribbon, QStringLiteral("ribbonAction_") + entry->id()) != nullptr,
                     qPrintable(entry->id()));
        }
        QVERIFY(findItem(ribbon, QStringLiteral("fontFamilyCombo")) != nullptr);
        QVERIFY(findItem(ribbon, QStringLiteral("fontSizeCombo")) != nullptr);
        // The button is the entry: triggering it toggles the entry and the
        // entry's state is what the button shows.
        QQuickItem *bold = findItem(ribbon, QStringLiteral("ribbonAction_") + format.bold->id());
        scene.selectFirstWord();
        QVERIFY(!bold->property("checked").toBool());
        QTest::mouseClick(scene.window, Qt::LeftButton, {},
                          bold->mapToScene(QPointF(bold->width() / 2, bold->height() / 2)).toPoint());
        QTRY_VERIFY(format.bold->isChecked());
        QTRY_VERIFY(bold->property("checked").toBool());
        // The Table tab is contextual: absent until the cursor is in a table.
        QVERIFY(!ribbon->property("contextIds").toStringList().contains(QStringLiteral("table")));
        scene.host.insertTable(2, 2);
        QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("table")));
    }

    void appMenusExist()
    {
        WriteHost host;
        const MenuNode *formatMenu = host.menuTree()->find(QStringLiteral("formatMenu"));
        const MenuNode *insertMenu = host.menuTree()->find(QStringLiteral("insertMenu"));
        QVERIFY2(formatMenu != nullptr, "Format menu must exist");
        QVERIFY2(insertMenu != nullptr, "Insert menu must exist");
        const WriteActions &format = host.formatController()->actions();
        QVERIFY(menuContains(formatMenu, format.bold->id()));
        QVERIFY(menuContains(insertMenu, format.insertColumns->id()));
        QVERIFY(menuContains(insertMenu, format.insertTable->id()));
        QVERIFY(host.menuTree()->find(QStringLiteral("tableMenu")) != nullptr);
        QVERIFY(host.menuTree()->find(QStringLiteral("writeReviewMenu")) != nullptr);
        QVERIFY(host.menuTree()->find(QStringLiteral("pageMenu")) != nullptr);
    }

    void charFormatActionsApplyAndSync()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        const WriteActions &actions = scene.host.formatController()->actions();

        actions.bold->trigger();
        QTextCursor cursor = scene.control()->textCursor();
        QCOMPARE(cursor.charFormat().fontWeight(), int(QFont::Bold));
        QVERIFY(actions.bold->isChecked());

        actions.italic->trigger();
        cursor = scene.control()->textCursor();
        QVERIFY(cursor.charFormat().fontItalic());
        QVERIFY(actions.italic->isChecked());

        actions.underline->trigger();
        QVERIFY(scene.control()->textCursor().charFormat().fontUnderline());
        actions.strikeOut->trigger();
        QVERIFY(scene.control()->textCursor().charFormat().fontStrikeOut());

        // Toggling bold off leaves the others alone.
        actions.bold->trigger();
        cursor = scene.control()->textCursor();
        QCOMPARE(cursor.charFormat().fontWeight(), int(QFont::Normal));
        QVERIFY(cursor.charFormat().fontItalic());
    }

    void fontControlsApplyAndSync()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        const QStringList installed = QFontDatabase::families();
        QVERIFY(!installed.isEmpty());
        scene.host.applyFontFamily(installed.first());
        QTextCursor cursor = scene.control()->textCursor();
        QVERIFY(cursor.charFormat().font().families().contains(installed.first()));
        QCOMPARE(scene.host.fontFamily(), installed.first());

        scene.host.applyFontSize(QStringLiteral("26"));
        cursor = scene.control()->textCursor();
        QCOMPARE(cursor.charFormat().fontPointSize(), 26.0);
        // The control syncs the combo's source back through the host.
        QCOMPARE(scene.host.fontSize(), QStringLiteral("26"));
        // The strip builds on the next event-loop pass (coalesced rebuild).
        QQuickItem *sizeCombo = nullptr;
        QTRY_VERIFY((sizeCombo = scene.item(QStringLiteral("fontSizeCombo"))) != nullptr);
        QTRY_COMPARE(sizeCombo->property("currentText").toString(), QStringLiteral("26"));
    }

    void alignmentAndListsApply()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        const WriteActions &actions = scene.host.formatController()->actions();
        QTextCursor cursor = scene.control()->textCursor();

        actions.alignCenter->trigger();
        QCOMPARE(cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignHCenter);
        QVERIFY(actions.alignCenter->isChecked());
        actions.alignJustify->trigger();
        QCOMPARE(cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignJustify);
        QVERIFY(actions.alignJustify->isChecked());
        QVERIFY(!actions.alignCenter->isChecked());

        actions.listBullet->trigger();
        cursor = scene.control()->textCursor();
        QVERIFY(cursor.block().textList() != nullptr);
        QCOMPARE(cursor.block().textList()->format().style(), QTextListFormat::ListDisc);
        QVERIFY(actions.listBullet->isChecked());
        // Bullet -> numbered converts instead of nesting.
        actions.listNumbered->trigger();
        cursor = scene.control()->textCursor();
        QVERIFY(cursor.block().textList() != nullptr);
        QCOMPARE(cursor.block().textList()->format().style(), QTextListFormat::ListDecimal);
        QVERIFY(actions.listNumbered->isChecked());
        QVERIFY(!actions.listBullet->isChecked());
        // Toggling the active style removes the list.
        actions.listNumbered->trigger();
        cursor = scene.control()->textCursor();
        QVERIFY(cursor.block().textList() == nullptr);
        QVERIFY(!actions.listNumbered->isChecked());
    }

    void indentChangesBlockFormat()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        const WriteActions &actions = scene.host.formatController()->actions();
        actions.indentMore->trigger();
        QCOMPARE(scene.control()->textCursor().blockFormat().indent(), 1);
        actions.indentLess->trigger();
        QCOMPARE(scene.control()->textCursor().blockFormat().indent(), 0);
        actions.indentLess->trigger(); // never negative
        QCOMPARE(scene.control()->textCursor().blockFormat().indent(), 0);
    }

    void tableAndColumnsInsert()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        scene.host.insertTable(2, 3);
        QTextDocument *doc = scene.doc->textDocument();
        QTextTable *table = nullptr;
        for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
            if ((table = qobject_cast<QTextTable *>(frame)) != nullptr) {
                break;
            }
        }
        QVERIFY(table != nullptr);
        QCOMPARE(table->rows(), 2);
        QCOMPARE(table->columns(), 3);
        QVERIFY(table->format().border() >= 1);
        // Columns: a borderless 1xN table (QTextDocument has no native
        // text-column sections).
        scene.selectFirstWord();
        scene.host.insertColumns(3, 24);
        QTextTable *columns = nullptr;
        for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
            if (auto *candidate = qobject_cast<QTextTable *>(frame);
                candidate != nullptr && candidate->rows() == 1 && candidate->columns() == 3) {
                columns = candidate;
            }
        }
        QVERIFY(columns != nullptr);
        QCOMPARE(columns->format().border(), 0.0);
        QCOMPARE(columns->format().cellPadding(), 12.0);
    }

    void findAndReplaceFlows()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("alpha beta gamma beta delta"));
        QQuickItem *bar = scene.item(QStringLiteral("findBar"));
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->isVisible());
        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kEditFind)));
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kEditReplace)));
        QTRY_VERIFY(findItem(bar, QStringLiteral("replaceRow"))->isVisible());

        QVERIFY(scene.host.findNext(QStringLiteral("beta"), false));
        QCOMPARE(scene.control()->textCursor().selectedText(), QStringLiteral("beta"));
        QCOMPARE(scene.control()->textCursor().selectionStart(), 6);
        // Wrap-around: second match, then back to the first.
        QVERIFY(scene.host.findNext(QStringLiteral("beta"), false));
        QCOMPARE(scene.control()->textCursor().selectionStart(), 17);
        QVERIFY(scene.host.findNext(QStringLiteral("beta"), false));
        QCOMPARE(scene.control()->textCursor().selectionStart(), 6);
        QVERIFY(!scene.host.findNext(QStringLiteral("zeta"), false));

        QCOMPARE(scene.host.replaceAll(QStringLiteral("beta"), QStringLiteral("BETA"), false), 2);
        const QString text = scene.doc->textDocument()->toPlainText();
        QVERIFY(!text.contains(QStringLiteral(" beta ")));
        QCOMPARE(text.count(QStringLiteral("BETA")), 2);
        // One undo step for the whole sweep.
        scene.control()->undo();
        QCOMPARE(scene.doc->textDocument()->toPlainText().count(QStringLiteral("beta")), 2);
    }

    void editActionsFollowControlAvailability()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        ActionEntry *undo = scene.host.action(sid(ActionId::kEditUndo));
        ActionEntry *copy = scene.host.action(sid(ActionId::kEditCopy));
        QVERIFY(!undo->isEnabled());
        QVERIFY(!copy->isEnabled());
        // A pristine document has no undo history (the setPageSize quirk).
        QCOMPARE(scene.doc->textDocument()->availableUndoSteps(), 0);
        scene.selectFirstWord();
        QCoreApplication::processEvents();
        QVERIFY(undo->isEnabled());
        QVERIFY(copy->isEnabled());
        undo->trigger();
        QCOMPARE(scene.doc->textDocument()->toPlainText(), QString());
    }

    void paperPaletteIsPinned()
    {
        // D-004 governs chrome, not paper: the page renders light with dark
        // default text under any desktop theme.
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const QPalette paper = scene.control()->paperPalette();
        QCOMPARE(paper.color(QPalette::Base), QColor(Qt::white));
        QCOMPARE(paper.color(QPalette::Text), QColor(Qt::black));
        QTest::qWait(100);
        const QImage grab = scene.window->grabWindow();
        QVERIFY(!grab.isNull());
        // A point inside the first page's top margin, in window pixels.
        const QPointF inPage = scene.page->mapToScene(QPointF(200, 24 + 40));
        QCOMPARE(QColor(grab.pixel(inPage.toPoint())), QColor(Qt::white));
    }

    void zoomActionsAdjustView()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *viewport = scene.viewport();
        QVERIFY(viewport != nullptr);
        ActionEntry *zoomIn = scene.host.action(sid(ActionId::kViewZoomIn));
        QVERIFY(zoomIn->isEnabled());
        const qreal before = viewport->property("zoom").toReal();
        zoomIn->trigger();
        QTRY_VERIFY(viewport->property("zoom").toReal() > before);
        scene.host.action(sid(ActionId::kViewZoomReset))->trigger();
        QTRY_COMPARE(viewport->property("zoom").toReal(), 1.0);
        QTRY_VERIFY(scene.host.statusInfo().contains(QStringLiteral("Zoom: 100%")));
    }

    void tableOperationsWork()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.selectFirstWord();
        scene.host.insertTable(2, 2);
        TableController *tables = scene.host.tableController();
        QVERIFY(tables->actions().insertRowBelow->isEnabled());
        QVERIFY(tables->actions().mergeCells->isEnabled());
        QTextTable *table = scene.control()->textCursor().currentTable();
        QVERIFY(table != nullptr);
        tables->actions().insertRowBelow->trigger();
        QCOMPARE(table->rows(), 3);
        tables->actions().insertColumnRight->trigger();
        QCOMPARE(table->columns(), 3);
        // Merge the cursor cell with its right neighbour, then split back.
        tables->actions().mergeCells->trigger();
        QVERIFY(table->cellAt(scene.control()->textCursor()).columnSpan() >= 2);
        QVERIFY(tables->actions().splitCell->isEnabled());
        tables->actions().splitCell->trigger();
        QCOMPARE(table->cellAt(scene.control()->textCursor()).columnSpan(), 1);
        tables->actions().deleteRow->trigger();
        QCOMPARE(table->rows(), 2);
        tables->actions().deleteColumn->trigger();
        QCOMPARE(table->columns(), 2);
        // Moving the cursor out of the table disables the ops.
        QTextCursor outside(scene.doc->textDocument());
        outside.movePosition(QTextCursor::End);
        scene.control()->setTextCursor(outside);
        QVERIFY(!tables->actions().insertRowBelow->isEnabled());
    }

    void pageSetupRoundTripsThroughHtml()
    {
        WriteHost host;
        host.writeDocument()->textDocument()->setPlainText(QStringLiteral("body"));
        host.applyPageSetup(816, 1056, 64, QStringLiteral("Report & <Quotes>"), QStringLiteral("Page {page} of {pages}"));
        QCOMPARE(host.pageSetup().value(QStringLiteral("margin")).toReal(), 64.0);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/doc.html");
        QString error;
        QVERIFY2(host.writeDocument()->save(path, &error), qPrintable(error));
        QFile raw(path);
        QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString html = QString::fromUtf8(raw.readAll());
        QVERIFY(html.contains(QStringLiteral("qindawrite-page")));
        QVERIFY(html.contains(QStringLiteral("qindawrite-header")));
        QVERIFY(html.contains(QStringLiteral("&amp;")));
        WriteDocument reloaded;
        QVERIFY2(reloaded.load(path, &error), qPrintable(error));
        QCOMPARE(reloaded.pageSize(), QSizeF(816, 1056));
        QCOMPARE(reloaded.pageMargin(), 64.0);
        QCOMPARE(reloaded.headerText(), QStringLiteral("Report & <Quotes>"));
        QCOMPARE(reloaded.footerText(), QStringLiteral("Page {page} of {pages}"));
        QCOMPARE(formatPageDecoration(QStringLiteral("{page}/{pages}"), 2, 7), QStringLiteral("2/7"));
    }

    // The ribbon gate (tests/support/ribbon_gate.h) at a desktop width and
    // squeezed: every menu command reachable, every icon real, every item
    // of every tab visible inside the ribbon.
    void ribbonReachesEveryCommandAndSqueezes()
    {
        for (const int width : {1400, 520}) {
            Scene scene(QSize(width, 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(),
                // The colour dialogs are the colour buttons' "Custom…" (the slots).
                {QStringLiteral("formatTextColorAction"), QStringLiteral("formatHighlightAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void freshHostIsPristineSoOpenCanReuseIt()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        // AGENT-CONTRACT (ADR-0110): a freshly created host is PRISTINE, which
        // lets File ▸ Open replace its blank document. Anything that dirties
        // the document during construction (the spell checker's first pass,
        // a default-font apply) breaks that silently.
        QVERIFY(!scene.doc->isDirty());
        QVERIFY(scene.host.isPristineUntitled());
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_%1").arg(width));
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindawrite_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

WRITE_TEST_MAIN(WriteUiTest)

#include "tst_write_ui.moc"
