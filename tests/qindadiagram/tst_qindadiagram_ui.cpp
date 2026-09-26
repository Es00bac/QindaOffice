// SPDX-License-Identifier: GPL-3.0-or-later
// QindaDiagram through the real window (M50): three seeded drawings — a
// volunteer-onboarding flowchart across swimlanes, a small nonprofit's org
// chart built from CSV, and an office network — render on the canvas;
// the Shapes panel searches across stencils; a selection fills the
// inspector; a drag from the panel drops a shape where it is released; the
// connector tool glues; keyboard nudges and undo; the ribbon gate at 1320
// and 520 px; structural fixtures. QQO_GRAB_DIR saves PNGs of each view.

#include "diagram_app_test_support.h"

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
            for (const char *tab : {"insert", "design", "arrange", "shape", "connector"}) {
                QVariant ok;
                QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, str(tab)));
                QVERIFY2(ok.toBool(), tab);
                QTest::qWait(100);
                QindaOffice::Test::saveGrab(scene.window, str("qindadiagram_tab_%1_1320").arg(str(tab)));
            }
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
