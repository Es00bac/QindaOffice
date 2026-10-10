// SPDX-License-Identifier: GPL-3.0-or-later
// QindaDiagram through the real window (M50): three seeded drawings — a
// volunteer-onboarding flowchart across swimlanes, a small nonprofit's org
// chart built from CSV, and an office network — render on the canvas;
// the Shapes panel searches across stencils; a selection fills the
// inspector; a drag from the panel drops a shape where it is released; the
// connector tool glues; keyboard nudges and undo; the ribbon gate at 1320
// and 520 px; structural fixtures; a Visio drawing opened in the window
// (M65, D-073; QQO_VISIO_UI_FILE swaps in a real .vsdx for a manual look).
// QQO_GRAB_DIR saves PNGs of each view.

#include "diagram_app_test_support.h"

#include "diagram_engine/visio_fixture.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace DiagramAppTest;
using QindaOffice::Test::findNamed;

namespace {

struct Scene {
    QQmlApplicationEngine engine;
    DiagramHost host;
    QQuickWindow *window = nullptr;
    QString error;

    explicit Scene(const QSize &size, bool seed = true)
    {
        if (seed) {
            DiagramService *d = host.diagram();
            seedFlowchart(d);
            seedOrgChart(d);
            seedNetwork(d);
            d->setCurrentPageId(d->pages().first().toMap().value(str("id")).toString());
        }
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(str("qrc:/qt/qml/QindaOffice/Diagram/App/DiagramWindow.qml")));
        QObject *object = component.createWithInitialProperties({{str("host"), QVariant::fromValue(&host)}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host.attachWindow(window);
        window->resize(size);
        window->show();
    }
    ~Scene() { delete window; }

    [[nodiscard]] QQuickItem *item(const QString &name) const { return findNamed(window->contentItem(), name); }
    void settle(int ms = 250) const
    {
        QTest::qWait(ms);
        // Fit the page after the layout has its final size.
        QMetaObject::invokeMethod(item(str("diagramCanvasView")), "zoom", Q_ARG(QVariant, str("page")));
        QTest::qWait(120);
    }
    void showPage(int index)
    {
        DiagramService *d = host.diagram();
        d->setCurrentPageId(d->pages().at(index).toMap().value(str("id")).toString());
        settle();
    }
};

// QTest::keyClicks has no QWindow overload.
void typeText(QQuickWindow *window, const QString &text)
{
    for (const QChar ch : text) {
        QTest::keyClick(window, ch.toLatin1());
    }
}

QString grabName(const char *view, int width)
{
    return str("qindadiagram_%1_%2").arg(str(view)).arg(width);
}

} // namespace

class DiagramUiTest final : public QObject {
    Q_OBJECT
private slots:
    void drawingsRenderAtBothWidths()
    {
        for (const int width : {1320, 520}) {
            Scene scene(QSize(width, width == 1320 ? 840 : 760));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.settle();
            QQuickItem *canvas = scene.item(str("diagramCanvas"));
            QVERIFY(canvas != nullptr);
            QVERIFY(canvas->property("zoom").toDouble() > 0.2);
            QindaOffice::Test::saveGrab(scene.window, grabName("flowchart", width));
            scene.showPage(1);
            QindaOffice::Test::saveGrab(scene.window, grabName("orgchart", width));
            scene.showPage(2);
            // The minimap (off by default) over the network page.
            scene.host.setMinimapVisible(true);
            QTest::qWait(150);
            QVERIFY(scene.item(str("diagramMinimap"))->isVisible() == (width > 900));
            QindaOffice::Test::saveGrab(scene.window, grabName("network", width));
            QCOMPARE(scene.host.diagram()->pages().size(), 3);
            // Narrow windows turn the panels into drawers.
            QCOMPARE(scene.item(str("diagramShapesSlot"))->isVisible(), width > 900);
            QCOMPARE(scene.item(str("diagramDrawerButtons"))->isVisible(), width < 900);
        }
    }

    void visioDrawingRenders()
    {
        QTemporaryDir dir;
        QString path = qEnvironmentVariable("QQO_VISIO_UI_FILE");
        if (path.isEmpty()) {
            path = dir.filePath(str("plan.vsdx"));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(VisioFixture::officePackage());
        }
        Scene scene(QSize(1320, 840), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(scene.host.loadPath(path));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.settle();
        QVERIFY(!scene.host.diagram()->pages().isEmpty());
        QVERIFY(scene.item(str("diagramCanvas"))->property("zoom").toDouble() > 0.1);
        QindaOffice::Test::saveGrab(scene.window, grabName("visio", 1320));
    }

    void selectionFillsTheInspector()
    {
        Scene scene(QSize(1320, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.settle();
        DiagramService *d = scene.host.diagram();
        QCOMPARE(d->find(str("Background check")), 1);
        QCOMPARE(d->selection().size(), 1);
        QTest::qWait(150);
        QCOMPARE(scene.item(str("diagramInspectorTitle"))->property("text").toString(), str("Decision"));
        QVERIFY(scene.item(str("diagramFillEditor"))->isVisible());
        QVERIFY(scene.host.ribbonContexts().contains(str("shape")));
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_selection_inspector_1320"));
        // The Data section of the network's server.
        scene.showPage(2);
        d->find(str("File server"));
        scene.host.actions()->trigger(str("diagramShapeDataAction"));
        QTest::qWait(150);
        QVERIFY(scene.item(str("diagramInspectorData"))->isVisible());
        QVERIFY(scene.item(str("diagramDataRow_0")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_shape_data_1320"));
        // A connector selection offers the Connector tab.
        scene.showPage(0);
        d->find(str("Yes"));
        scene.item(str("diagramInspector"))->setProperty("section", str("style"));
        QTest::qWait(100);
        QVERIFY(scene.host.ribbonContexts().contains(str("connector")));
        QVERIFY(scene.item(str("diagramRoutingSegments"))->isVisible());
    }

    void shapesPanelSearchesEveryStencil()
    {
        Scene scene(QSize(1320, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.settle();
        QQuickItem *palette = scene.item(str("diagramShapePalette"));
        QVERIFY(palette != nullptr);
        QVERIFY(palette->property("definitions").toList().size() >= 45);
        // Type into the panel's search field as a user would.
        QQuickItem *search = findNamed(palette, str("shapeSearch"));
        QVERIFY(search != nullptr);
        search->forceActiveFocus();
        typeText(scene.window, str("data"));
        QTest::qWait(250);
        QCOMPARE(palette->property("searchText").toString(), str("data"));
        const QVariantList matches = palette->property("visibleDefinitions").toList();
        QVERIFY2(matches.size() >= 3, "Data, Database, Database server …");
        QVERIFY(scene.item(str("shape_networkDatabase")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_shape_search_1320"));
        search->setProperty("text", QString()); // focused: the palette follows
        QCOMPARE(palette->property("searchText").toString(), QString());
        palette->setProperty("category", str("Network"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_network_stencils_1320"));
    }

    void dragFromThePanelConnectAndNudge()
    {
        Scene scene(QSize(1320, 840), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.settle();
        DiagramService *d = scene.host.diagram();
        QQuickItem *canvas = scene.item(str("diagramCanvas"));
        // A drag from the panel, released over the page, drops there.
        const QPointF drop = canvas->mapToScene(QPointF(canvas->width() * 0.3, canvas->height() * 0.4));
        QMetaObject::invokeMethod(scene.window, "dragFinished", Q_ARG(QVariant, str("process")), Q_ARG(QVariant, drop));
        QCOMPARE(d->pageInfo().value(str("shapeCount")).toInt(), 1);
        const QString first = d->selection().value(0);
        QPointF docPoint;
        QMetaObject::invokeMethod(canvas, "mapToDocument", Q_RETURN_ARG(QPointF, docPoint),
                                  Q_ARG(QPointF, QPointF(canvas->width() * 0.3, canvas->height() * 0.4)));
        const QVariantMap item = d->item(first);
        const QPointF centre(item.value(str("x")).toDouble() + item.value(str("width")).toDouble() / 2,
                             item.value(str("y")).toDouble() + item.value(str("height")).toDouble() / 2);
        QVERIFY(QLineF(centre, docPoint).length() < 8); // grid-snapped drop
        // A second shape by click (the view centre), then the connector tool.
        const QString second = d->addShape(str("decision"), centre.x() + 260, centre.y());
        scene.host.actions()->trigger(str("diagramToolConnectorAction"));
        QCOMPARE(d->tool(), str("connector"));
        QPointF a;
        QPointF b;
        QMetaObject::invokeMethod(canvas, "mapFromDocument", Q_RETURN_ARG(QPointF, a), Q_ARG(QPointF, centre));
        QMetaObject::invokeMethod(canvas, "mapFromDocument", Q_RETURN_ARG(QPointF, b),
                                  Q_ARG(QPointF, QPointF(centre.x() + 260, centre.y())));
        QTest::mousePress(scene.window, Qt::LeftButton, {}, canvas->mapToScene(a).toPoint());
        for (int i = 1; i <= 6; ++i) {
            const QPointF p = a + (b - a) * (i / 6.0);
            QTest::mouseMove(scene.window, canvas->mapToScene(p).toPoint());
        }
        QTest::mouseRelease(scene.window, Qt::LeftButton, {}, canvas->mapToScene(b).toPoint());
        QTRY_COMPARE(d->pageInfo().value(str("connectorCount")).toInt(), 1);
        const QVariantMap connector = d->selected();
        QCOMPARE(connector.value(str("kind")).toString(), str("connector"));
        QVERIFY(connector.value(str("fromGlued")).toBool());
        QVERIFY(connector.value(str("toGlued")).toBool());
        scene.host.actions()->trigger(str("diagramToolPointerAction"));
        // Keyboard nudge (one undo step for the run), then undo it.
        d->setSelection({second});
        canvas->forceActiveFocus();
        const double x0 = d->item(second).value(str("x")).toDouble();
        QTest::keyClick(scene.window, Qt::Key_Right);
        QTest::keyClick(scene.window, Qt::Key_Right);
        QCOMPARE(d->item(second).value(str("x")).toDouble(), x0 + 12);
        scene.host.actions()->trigger(str("editUndoAction"));
        QCOMPARE(d->item(second).value(str("x")).toDouble(), x0);
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_new_drawing_1320"));
        // F2 edits the text in place; Escape stores it (one undo step).
        d->setSelection({second});
        scene.host.actions()->trigger(str("diagramEditTextAction"));
        QQuickItem *editor = scene.item(str("diagramTextEditor"));
        QVERIFY(editor != nullptr);
        QTRY_VERIFY(editor->isVisible());
        QTRY_VERIFY(editor->hasActiveFocus());
        typeText(scene.window, str("Approved?"));
        QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_text_edit_1320"));
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QTRY_VERIFY(!editor->isVisible());
        QCOMPARE(d->item(second).value(str("text")).toString(), str("Approved?"));
    }

    void ribbonGateHolds()
    {
        for (const int width : {1320, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_ribbon_%1").arg(width));
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(),
                // The assistant has nothing to offer a drawing yet (disabled,
                // AI menu only); the paper sizes, themes and quick styles are
                // dropdown entries the gate counts through their menus.
                {str("aiAssistantAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
            if (width != 1320) {
                continue;
            }
            // The other tabs, for review; Shape with a shape selected.
            scene.host.diagram()->selectAll();
            QQuickItem *ribbon = scene.item(str("officeRibbon"));
            for (const char *tab : {"insert", "design", "arrange", "data", "review", "shape", "connector", "container"}) {
                if (qstrcmp(tab, "container") == 0) {
                    scene.host.diagram()->find(str("Coordinator")); // a lane: the Container tab (M87)
                }
                QVariant ok;
                QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, str(tab)));
                QVERIFY2(ok.toBool(), tab);
                QTest::qWait(100);
                QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_tab_%1_1320").arg(str(tab)));
            }
        }
    }

    // M87 (D-095): Visio's everyday tools in the real window — a pool with a
    // phase and a coloured layer, comment markers and the Comments pane,
    // page breaks, AutoConnect arrows over a hovered shape, and the new
    // dialogs (Layer Properties, Links, Layout Options, Link Data, External
    // Data, Page Setup with print and background settings) at both widths.
    void m87ToolsRender()
    {
        QTemporaryDir dir;
        const QString csv = dir.filePath(str("steps.csv"));
        {
            QFile file(csv);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("Step,Owner,Days\nReview application,Dana,2\nSchedule orientation,Sam,1\n");
        }
        for (const int width : {1320, 520}) {
            Scene scene(QSize(width, width == 1320 ? 840 : 760));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            DiagramService *d = scene.host.diagram();
            // The flowchart's lanes are one pool: a phase spans it.
            QVERIFY(!d->insertPhase(700, 300).isEmpty());
            d->addLayer(str("Review"));
            d->setLayerProperty(0, str("color"), str("#c00000"));
            d->find(str("Background check"));
            d->assignSelectionToLayers({0});
            d->addComment(str("Checks can take a week."));
            d->setSelection({});
            d->addComment(str("Confirm the lanes with the coordinator."));
            d->linkData(QUrl::fromLocalFile(csv), {}, 0, 0, {});
            scene.host.actions()->trigger(str("diagramShowPageBreaksAction"));
            d->setPrintSetup({{str("paper"), str("letter")}, {str("zoom"), 1}});
            scene.host.setCommentsPaneVisible(true);
            scene.settle();
            QQuickItem *pane = scene.item(str("diagramCommentsPane"));
            QVERIFY(pane != nullptr);
            QCOMPARE(pane->isVisible(), width > 900);
            QVERIFY(scene.item(str("commentCard_") + d->commentList().first().toMap().value(str("id")).toString()) != nullptr);
            // AutoConnect arrows over the hovered start shape.
            d->find(str("Interested in volunteering"));
            QQuickItem *view = scene.item(str("diagramPageView"));
            view->setProperty("autoConnectShape", d->selection().value(0));
            QTest::qWait(100);
            QVariantList arrows;
            QMetaObject::invokeMethod(view, "autoConnectArrows", Q_RETURN_ARG(QVariantList, arrows));
            QCOMPARE(arrows.size(), 4);
            QindaOffice::Test::saveGrab(scene.window, grabName("m87_swimlanes", width));
            view->setProperty("autoConnectShape", QString());
            for (const char *dialog : {"layers", "link", "layout", "externalData"}) {
                emit scene.host.dialogRequested(str(dialog));
                QTest::qWait(150);
                QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_m87_%1_%2").arg(str(dialog)).arg(width));
                QTest::keyClick(scene.window, Qt::Key_Escape);
                QTest::qWait(80);
            }
            QObject *linkData = scene.window->findChild<QObject *>(str("diagramLinkDataDialog"));
            QVERIFY(linkData != nullptr);
            QMetaObject::invokeMethod(linkData, "openWith", Q_ARG(QVariant, QUrl::fromLocalFile(csv)));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, grabName("m87_linkdata", width));
            QVERIFY(linkData->property("visible").toBool());
            QTest::keyClick(scene.window, Qt::Key_Escape);
            QTest::qWait(80);
            emit scene.host.pageSetupRequested();
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, grabName("m87_pagesetup", width));
            QTest::keyClick(scene.window, Qt::Key_Escape);
            QCOMPARE(d->recordsetList().size(), 1);
            QCOMPARE(d->commentTotal(), 2);
        }
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(200);
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindadiagram_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

DIAGRAM_APP_TEST_MAIN(DiagramUiTest)

#include "tst_qindadiagram_ui.moc"
