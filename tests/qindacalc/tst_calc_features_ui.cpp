// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075) through the real QindaCalc window: Insert ▸ Link, Edit and
// Remove Link, Ctrl+click and HYPERLINK() following through an injected
// opener (nothing is ever launched), internal links moving the cursor;
// Data ▸ Group and the outline gutter's +/- and level buttons; hidden rows
// and columns leaving the grid; Review ▸ Protect refusing edits to locked
// cells with a message, passwords checked, the workbook structure lock; a
// hidden formula; spills in the grid. QQO_GRAB_DIR saves the windows.

#include "calc_test_support.h"

#include "standard_actions.h"
#include "support/quick_dump.h"

#include "hyperlink.h"
#include "sheet.h"

using namespace CalcTest;
using namespace QindaOffice::Calc;
namespace ActionId = QindaOffice::Quick::ActionId;

class CalcFeaturesUiTest : public QObject {
    Q_OBJECT

    static void trigger(Scene &scene, const char *id) { scene.host.actions()->trigger(QString::fromLatin1(id)); }
    // A Tk.Dialog is a Popup (a QObject child of the window): wait for its
    // enter transition before a grab, or the PNG shows only the dimmer.
    static bool dialogOpened(Scene &scene, const char *name)
    {
        QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
        if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
            return false;
        }
        QTest::qWait(300);
        return true;
    }
    // AGENT-GUARD: never hold a Sheet& across an undo — restoreSnapshot
    // move-assigns a fresh Workbook, so the old sheets are gone.
    static const Sheet &sheet0(Scene &scene) { return scene.doc->workbook()->sheetAt(0); }

private slots:
    void linksFollowedThroughTheOpener()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QList<QUrl> opened;
        scene.host.setUrlOpener([&opened](const QUrl &url) {
            opened.append(url);
            return true;
        });
        SpreadsheetDocument *doc = scene.doc;
        doc->insertSheet(QStringLiteral("Sheet 2"));
        scene.host.setActiveSheet(0);
        scene.grid->setCursorCell(0, 0);
        scene.host.applyLink({{QStringLiteral("address"), QStringLiteral("www.example.com")},
                              {QStringLiteral("display"), QStringLiteral("Example")},
                              {QStringLiteral("tooltip"), QStringLiteral("Our site")}});
        QVERIFY(sheet0(scene).hyperlinkAt({0, 0}) != nullptr);
        QCOMPARE(sheet0(scene).hyperlinkAt({0, 0})->address, QStringLiteral("http://www.example.com"));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("Example"));
        const CellStyle *look = doc->workbook()->style(sheet0(scene).styleIdAt({0, 0}));
        QVERIFY(look != nullptr && look->underline && look->fg.has_value());
        QCOMPARE(scene.host.linkAtCursor().value(QStringLiteral("tooltip")).toString(), QStringLiteral("Our site"));
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcEditHyperlink))->isEnabled());
        // Ctrl+click on the cell follows it; a plain click only selects.
        const QPointF a1 = scene.grid->mapToScene(scene.grid->cursorItemRect().center());
        scene.grid->setCursorCell(3, 3);
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier, a1.toPoint());
        QCOMPARE(scene.grid->cursorRow(), 0);
        QVERIFY(opened.isEmpty());
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::ControlModifier, a1.toPoint());
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first(), QUrl(QStringLiteral("http://www.example.com")));
        // An internal link moves the cursor to its place.
        scene.grid->setCursorCell(1, 0);
        scene.host.applyLink({{QStringLiteral("sheet"), QStringLiteral("Sheet 2")}, {QStringLiteral("cell"), QStringLiteral("C4")},
                              {QStringLiteral("display"), QStringLiteral("To sheet 2")}});
        QCOMPARE(sheet0(scene).hyperlinkAt({1, 0})->location, QStringLiteral("'Sheet 2'!C4"));
        QVERIFY(scene.host.openLinkAt(1, 0));
        QCOMPARE(doc->activeSheet(), 1);
        QCOMPARE(scene.grid->cursorRow(), 3);
        QCOMPARE(scene.grid->cursorCol(), 2);
        scene.host.setActiveSheet(0);
        // HYPERLINK(): clickable, its location evaluated where it lives.
        doc->applyInput({2, 1}, QStringLiteral("orders"));
        doc->applyInput({2, 0}, QStringLiteral("=HYPERLINK(\"https://example.com/\"&B3,\"Orders\")"));
        QVERIFY(scene.host.openLinkAt(2, 0));
        QCOMPARE(opened.last(), QUrl(QStringLiteral("https://example.com/orders")));
        // Remove Link takes the link and its look.
        scene.grid->setCursorCell(0, 0);
        trigger(scene, ActionId::kCalcRemoveHyperlink);
        QVERIFY(sheet0(scene).hyperlinkAt({0, 0}) == nullptr);
        const CellStyle *plain = doc->workbook()->style(sheet0(scene).styleIdAt({0, 0}));
        QVERIFY(plain == nullptr || (!plain->underline && !plain->fg.has_value()));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("Example"));
        // A program is never started from a link.
        scene.grid->setCursorCell(4, 0);
        scene.host.applyLink({{QStringLiteral("address"), QStringLiteral("javascript:alert(1)")}});
        const int before = int(opened.size());
        QVERIFY(scene.host.openLinkAt(4, 0));
        QCOMPARE(int(opened.size()), before);
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("does not open")));
        // The link dialog opens on Insert ▸ Link with the cursor's link.
        scene.grid->setCursorCell(1, 0);
        trigger(scene, ActionId::kInsertHyperlink);
        QVERIFY(dialogOpened(scene, "linkDialog"));
        QVERIFY(scene.item(QStringLiteral("linkCell")) != nullptr && scene.item(QStringLiteral("linkCell"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m66_link_dialog"));
    }

    void outlineGroupsAndTheGutter()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        for (int r = 0; r < 8; ++r) {
            doc->applyInput({r, 0}, QString::number(r + 1));
        }
        QCOMPARE(scene.grid->rowGutterPx(), 0.0);
        // Rows 2-4 (selected from the row headers) grouped.
        scene.grid->selectRow(1, false);
        scene.grid->selectRow(3, true);
        trigger(scene, ActionId::kCalcGroup);
        QCOMPARE(sheet0(scene).outline().maxLevel(OutlineAxis::Rows), quint8(1));
        QVERIFY(scene.grid->rowGutterPx() > 0);
        QVERIFY(scene.grid->headerWidth() > SpreadsheetItem::kHeaderWidth);
        // Hide Detail on the cursor's group: rows 2-4 leave the grid.
        scene.grid->setCursorCell(2, 0);
        trigger(scene, ActionId::kCalcHideDetail);
        QVERIFY(sheet0(scene).isRowHidden(1) && sheet0(scene).isRowHidden(3));
        QVERIFY(!scene.grid->visibleRows().contains(2));
        QVERIFY(scene.grid->visibleRows().contains(4));
        QTest::qWait(50);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m66_outline_collapsed"));
        // The gutter's + button (on the summary row 5) shows the detail again.
        QSignalSpy toggled(scene.grid, &SpreadsheetItem::outlineToggled);
        const qreal step = SpreadsheetItem::kGutterStep;
        const qreal summaryY = scene.grid->headerHeight() + scene.grid->rowTopForTest(4) + 10;
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier,
                          scene.grid->mapToScene(QPointF(step / 2, summaryY)).toPoint());
        QCOMPARE(toggled.size(), 1);
        QVERIFY(!sheet0(scene).isRowHidden(2));
        // Level button 1 collapses every group; one undo step brings them back.
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier,
                          scene.grid->mapToScene(QPointF(step / 2, scene.grid->colGutterPx() + 12)).toPoint());
        QVERIFY(sheet0(scene).isRowHidden(1));
        doc->undoStack()->undo();
        QVERIFY(!sheet0(scene).isRowHidden(1));
        // Columns grouped from the column headers; ungroup peels the level.
        scene.grid->selectColumn(2, false);
        trigger(scene, ActionId::kCalcGroup);
        QCOMPARE(sheet0(scene).outline().maxLevel(OutlineAxis::Columns), quint8(1));
        QVERIFY(scene.grid->colGutterPx() > 0);
        trigger(scene, ActionId::kCalcUngroup);
        QCOMPARE(sheet0(scene).outline().maxLevel(OutlineAxis::Columns), quint8(0));
        // Hide / Unhide Columns, and the cursor steps over hidden columns.
        scene.grid->setCursorCell(0, 1);
        scene.grid->setCursorCell(0, 2, true);
        trigger(scene, ActionId::kCalcHideColumns);
        QVERIFY(!scene.grid->visibleColumns().contains(1));
        scene.grid->setCursorCell(0, 0);
        scene.grid->forceActiveFocus();
        QTest::keyClick(scene.window, Qt::Key_Right);
        QCOMPARE(scene.grid->cursorCol(), 3);
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(0, 3, true);
        trigger(scene, ActionId::kCalcUnhideColumns);
        QVERIFY(scene.grid->visibleColumns().contains(1));
    }

    void protectionRefusesLockedCells()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window)); // laid out before the cursor moves
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("5"));
        doc->applyInput({0, 2}, QStringLiteral("=A1*2"));
        // B1 unlocked, C1's formula hidden — before protecting.
        scene.grid->setCursorCell(0, 1);
        scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcLockCell))->setChecked(false);
        scene.grid->setCursorCell(0, 2);
        scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcHideFormula))->setChecked(true);
        scene.host.protectSheetWith({{QStringLiteral("password"), QStringLiteral("s3cret")},
                                     {QStringLiteral("formatCells"), false}});
        QVERIFY(sheet0(scene).protection().enabled);
        QCOMPARE(sheet0(scene).protection().algorithmName, QStringLiteral("SHA-512"));
        QVERIFY(!scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcProtectSheet))->isEnabled());
        // A locked cell refuses, and says why; the unlocked one takes input.
        doc->applyInput({0, 0}, QStringLiteral("9"));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("5"));
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("protected")));
        doc->applyInput({0, 1}, QStringLiteral("7"));
        QCOMPARE(doc->inputTextAt({0, 1}), QStringLiteral("7"));
        // The hidden formula shows its value only.
        QCOMPARE(doc->inputTextAt({0, 2}), QStringLiteral("10"));
        // Formatting, structure and rules are refused too.
        scene.grid->setCursorCell(0, 0);
        trigger(scene, ActionId::kFormatBold);
        const CellStyle *a1Style = doc->workbook()->style(sheet0(scene).styleIdAt({0, 0}));
        QVERIFY(a1Style == nullptr || !a1Style->bold);
        trigger(scene, ActionId::kCalcInsertRows);
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("5"));
        // The wrong password keeps the protection; the right one lifts it.
        QVERIFY(!scene.host.unprotectSheetWith(QStringLiteral("S3cret")));
        QVERIFY(sheet0(scene).protection().enabled);
        QVERIFY(scene.host.unprotectSheetWith(QStringLiteral("s3cret")));
        doc->applyInput({0, 0}, QStringLiteral("9"));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("9"));
        QCOMPARE(doc->inputTextAt({0, 2}), QStringLiteral("=A1*2"));
        // The workbook's structure lock: no new sheet until it is lifted.
        scene.host.protectWorkbookWith(QString());
        const int sheets = doc->workbook()->sheetCount();
        trigger(scene, ActionId::kInsertSheet);
        QCOMPARE(doc->workbook()->sheetCount(), sheets);
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("structure")));
        trigger(scene, ActionId::kCalcUnprotectWorkbook); // no password: lifted at once
        trigger(scene, ActionId::kInsertSheet);
        QCOMPARE(doc->workbook()->sheetCount(), sheets + 1);
        // The protect dialog, for the record.
        scene.host.setActiveSheet(0);
        trigger(scene, ActionId::kCalcProtectSheet);
        QVERIFY(dialogOpened(scene, "protectDialog"));
        QVERIFY(scene.item(QStringLiteral("protectPassword")) != nullptr
                && scene.item(QStringLiteral("protectPassword"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m66_protect_dialog"));
    }

    void spillsAndLinksInTheGrid()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        const QStringList regions{QStringLiteral("East"), QStringLiteral("West"), QStringLiteral("East"),
                                  QStringLiteral("North")};
        for (int r = 0; r < regions.size(); ++r) {
            doc->applyInput({r + 1, 0}, regions.at(r));
            doc->applyInput({r + 1, 1}, QString::number((r + 1) * 10));
        }
        doc->applyInput({0, 0}, QStringLiteral("Region"));
        doc->applyInput({0, 1}, QStringLiteral("Sales"));
        doc->applyInput({0, 3}, QStringLiteral("=UNIQUE(A2:A5)"));
        doc->applyInput({0, 4}, QStringLiteral("=SORT(A2:B5,2,-1)"));
        QCOMPARE(doc->displayTextAt({2, 3}), QStringLiteral("North"));
        QCOMPARE(doc->displayTextAt({0, 5}), QStringLiteral("40"));
        // A spilled cell knows its anchor (the grid borders the whole spill).
        scene.grid->setCursorCell(2, 4);
        QCOMPARE(doc->workbook()->spillAnchorOf(0, {2, 4}), (CellAddr{0, 4}));
        // Typing into the spill blocks it, Undo lets it spill again.
        doc->applyInput({1, 3}, QStringLiteral("x"));
        QCOMPARE(doc->displayTextAt({0, 3}), QStringLiteral("#SPILL!"));
        doc->undoStack()->undo();
        QCOMPARE(doc->displayTextAt({1, 3}), QStringLiteral("West"));
        scene.grid->setCursorCell(0, 6);
        scene.host.applyLink({{QStringLiteral("address"), QStringLiteral("https://example.com/report")},
                              {QStringLiteral("display"), QStringLiteral("Report")}});
        SheetOutline outline;
        groupLines(outline, OutlineAxis::Rows, 6, 8);
        groupLines(outline, OutlineAxis::Rows, 7, 7);
        doc->workbook()->sheetAt(0).setOutline(outline); // straight into the model: a grab only
        scene.grid->setCursorCell(1, 4);
        scene.grid->refreshAll();
        QTest::qWait(50);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m66_spill_links"));
    }

    // The new ribbon groups squeezed to 520 px (the ribbon gate in
    // tst_calc_ui checks every item; this is for the eye).
    void squeezedRibbonTabs()
    {
        Scene scene(QSize(520, 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        for (const QString &tab : {QStringLiteral("review"), QStringLiteral("data"), QStringLiteral("insert")}) {
            QVariant ok;
            QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, tab));
            QVERIFY2(ok.toBool(), qPrintable(tab));
            QTest::qWait(250);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m66_ribbon_%1_520").arg(tab));
        }
    }
};

CALC_TEST_MAIN(CalcFeaturesUiTest)

#include "tst_calc_features_ui.moc"
