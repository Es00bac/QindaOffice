// SPDX-License-Identifier: GPL-3.0-or-later
// The window's chrome: the ribbon's tool buttons carry their glyphs and are
// one radio group, the ribbon gate holds (every command reachable, nothing
// hidden when squeezed), the colour and width slots follow the active tool,
// and the navigator lists the notebook.

#include <QRegularExpression>
#include "note_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;

class NoteUiTest final : public QObject {
    Q_OBJECT

private slots:
    // Every case fails on a QML binding loop: the dock panel toggles used to
    // bounce between the View action and DockHost.hiddenPanels.
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void ribbonToolsAreAddressableAndExclusive()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *ribbon = findItem(scene.window->contentItem(), QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        for (const QString &id : {QStringLiteral("notePenTool"), QStringLiteral("noteHighlighterTool"),
                                  QStringLiteral("noteEraserTool"), QStringLiteral("noteTextTool"),
                                  QStringLiteral("noteAddTextAction"), QStringLiteral("noteAddCheckAction"),
                                  QStringLiteral("noteAddImageAction"), QStringLiteral("notePageNew")}) {
            QQuickItem *button = findItem(ribbon, QStringLiteral("ribbonAction_") + id);
            QVERIFY2(button != nullptr && button->isVisible(), qPrintable(id));
            // D-013: an icon that stops resolving must fail here.
            QVERIFY2(!button->property("iconName").toString().isEmpty(), qPrintable(id));
        }
        // The tool buttons are one radio group: pen checked, the rest not.
        QQuickItem *pen = findItem(ribbon, QStringLiteral("ribbonAction_notePenTool"));
        QQuickItem *eraser = findItem(ribbon, QStringLiteral("ribbonAction_noteEraserTool"));
        QVERIFY(pen->property("checked").toBool());
        QVERIFY(!eraser->property("checked").toBool());
        scene.host.actions()->trigger(QStringLiteral("noteEraserTool"));
        QTRY_VERIFY(eraser->property("checked").toBool());
        QVERIFY(!pen->property("checked").toBool());
    }

    void slotsFollowTheActiveTool()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *color = findItem(scene.window->contentItem(), QStringLiteral("noteColorButton"));
        QQuickItem *width = findItem(scene.window->contentItem(), QStringLiteral("noteWidthCombo"));
        QVERIFY(color != nullptr && width != nullptr);
        QVERIFY(color->isVisible());
        QCOMPARE(color->property("color").value<QColor>(), scene.page->penColor());
        scene.host.actions()->trigger(QStringLiteral("noteHighlighterTool"));
        QTRY_COMPARE(color->property("color").value<QColor>(), scene.page->highlighterColor());
        scene.host.actions()->trigger(QStringLiteral("noteEraserTool"));
        QTRY_VERIFY(!color->isVisible()); // the eraser has no colour
        QVERIFY(width->isVisible());
        scene.host.actions()->trigger(QStringLiteral("noteTextTool"));
        QTRY_VERIFY(!width->isVisible());
        // Picking a colour by command updates the item and the button.
        scene.host.actions()->trigger(QStringLiteral("notePenTool"));
        scene.host.actions()->trigger(QStringLiteral("notePenColor_red"));
        QTRY_COMPARE(color->property("color").value<QColor>(), scene.page->penColor());
        QVERIFY(scene.page->penColor() != QColor(Qt::black));
    }

    // The ribbon gate (tests/support/ribbon_gate.h, D-041) at a desktop
    // width and squeezed: every menu command reachable, every icon real,
    // every item of every tab (contextual ones forced on) inside the ribbon.
    void ribbonReachesEveryCommandAndSqueezes()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(), {});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void navigatorListsTheNotebookAndOpensPages()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.actions()->trigger(QStringLiteral("notePageNew"));
        const QString second = scene.host.currentPageId();
        const QString first = scene.host.noteDocument()->notebook()->sections().first().pages.first().id;
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(scene.window->contentItem(), QStringLiteral("navPage_") + first)) != nullptr);
        QVERIFY(findItem(scene.window->contentItem(), QStringLiteral("navPage_") + second) != nullptr);
        QVERIFY(row->isVisible());
        QTest::qWait(100); // let the column lay the fresh rows out
        // Clicking a row opens its page.
        const QPointF centre = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, centre.toPoint());
        QTRY_COMPARE(scene.host.currentPageId(), first);
        // View ▸ Show Navigator hides the dock panel.
        scene.host.actions()->trigger(QStringLiteral("noteShowNavigatorAction"));
        QTRY_VERIFY(!row->isVisible());
    }

    void assistantPanelFollowsTheAiMenu()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.assistant() != nullptr);
        // Hidden until asked for; AI ▸ Assistant… is checkable and live.
        QQuickItem *panel = findItem(scene.window->contentItem(), QStringLiteral("assistantPanel"));
        QVERIFY(panel == nullptr || !panel->isVisible());
        auto *entry = scene.host.actions()->entry(QStringLiteral("aiAssistantAction"));
        QVERIFY(entry != nullptr && entry->isEnabled() && entry->isCheckable());
        scene.host.actions()->trigger(QStringLiteral("aiAssistantAction"));
        QTRY_VERIFY((panel = findItem(scene.window->contentItem(), QStringLiteral("assistantPanel"))) != nullptr
                    && panel->isVisible());
        QTest::qWait(100);
        QVERIFY(findItem(panel, QStringLiteral("assistantPrompt")) != nullptr);
        QVERIFY(findItem(panel, QStringLiteral("assistantSend")) != nullptr);
        QVERIFY(!findItem(panel, QStringLiteral("assistantInsert"))->isEnabled());
        // A reply lands in a text block on the page (the seam, no provider).
        scene.host.setEditorSelection(QString());
        emit scene.host.assistantInsertRequested(QStringLiteral("from the assistant"));
        QTRY_VERIFY(scene.page->editingBlock() >= 0);
        QTRY_COMPARE(scene.page->editingText(), QStringLiteral("from the assistant"));
        scene.page->commitEdit();
        QVERIFY(scene.host.currentPageText().contains(QStringLiteral("from the assistant")));
        scene.host.actions()->trigger(QStringLiteral("aiAssistantAction"));
        QTRY_VERIFY(!panel->isVisible());
    }

    // The window's structure at a desktop width and squeezed to a third,
    // against tests/qml_fixtures (QQO_UPDATE_FIXTURES=1 rewrites them).
    void windowMatchesItsFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 760));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            // The dock layout is remembered per user (a feature); the
            // fixture is the DEFAULT layout, whatever an earlier test did.
            QQuickItem *dock = findItem(scene.window->contentItem(), QStringLiteral("noteDock"));
            QVERIFY(dock != nullptr);
            QVERIFY(QMetaObject::invokeMethod(dock, "resetLayout"));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_%1").arg(width));
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindanote_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

NOTE_TEST_MAIN(NoteUiTest)

#include "tst_note_ui.moc"
