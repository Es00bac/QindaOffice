// SPDX-License-Identifier: GPL-3.0-or-later
// QindaDiagram's M87 commands (D-095) through the host and its service,
// without a window: swimlanes joining their pool and Delete closing it,
// containers wrapping the selection and the Container ribbon context;
// layers hiding items from the canvas and from Select All; links followed
// to a page; comment threads walked across pages; Link Data to Shapes from
// a CSV file, refreshed after the file changes; AutoConnect and Re-Layout;
// Auto Size; a page printed across two sheets in the PDF. Each command is
// one undo step.

#include "diagram_app_test_support.h"

#include "qindadiagram/src/canvas_adapter.h"

#include "drawing_document.h"

#include <QFile>
#include <QRegularExpression>
#include <QUndoStack>

using namespace DiagramAppTest;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

int steps(DiagramHost &host)
{
    return host.drawingDocument()->undoStack()->count();
}

} // namespace

class DiagramM87Test final : public QObject {
    Q_OBJECT
private slots:
    void swimlanesAndContainers()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        host.setViewCenter(QPointF(500, 300));
        host.actions()->trigger(str("diagramInsertSwimlaneAction"));
        const QString first = d->selection().value(0);
        QVERIFY(d->item(first).value(str("lane")).toBool());
        QCOMPARE(host.ribbonContexts(), (QStringList{str("shape"), str("container")}));
        int before = steps(host);
        host.actions()->trigger(str("diagramInsertSwimlaneAction")); // after the selected lane
        QCOMPARE(steps(host), before + 1);
        const QString second = d->selection().value(0);
        QCOMPARE(d->item(second).value(str("y")).toDouble(),
                 d->item(first).value(str("y")).toDouble() + d->item(first).value(str("height")).toDouble());
        d->setSelection({second});
        host.actions()->trigger(str("diagramInsertSwimlaneBeforeAction"));
        const QString middle = d->selection().value(0);
        QCOMPARE(d->item(middle).value(str("y")).toDouble(), d->item(second).value(str("y")).toDouble()
                                                                 - d->item(middle).value(str("height")).toDouble());
        host.actions()->trigger(str("diagramInsertPhaseAction"));
        const QString phase = d->selection().value(0);
        QCOMPARE(d->item(phase).value(str("height")).toDouble(), d->item(first).value(str("height")).toDouble() * 3);
        // A step in the middle lane; deleting that lane closes the pool.
        const QVariantMap lane = d->item(middle);
        const QString step = d->addShape(str("process"), 400, lane.value(str("y")).toDouble() + 70);
        d->setSelection({middle});
        const double secondTop = d->item(second).value(str("y")).toDouble();
        QVERIFY(d->deleteSelection());
        QVERIFY(d->item(step).isEmpty());
        QCOMPARE(d->item(second).value(str("y")).toDouble(), secondTop - lane.value(str("height")).toDouble());
        host.actions()->trigger(str("editUndoAction")); // one step back: lane, step and the pool
        QVERIFY(!d->item(step).isEmpty());
        QCOMPARE(d->item(second).value(str("y")).toDouble(), secondTop);
        // Insert ▸ Container wraps the selection; Fit and Disband.
        const QString a = d->addShape(str("process"), 300, 700);
        const QString b = d->addShape(str("process"), 520, 760);
        d->setSelection({a, b});
        host.actions()->trigger(str("diagramInsertContainerAction"));
        const QString box = d->selection().value(0);
        QVERIFY(d->item(box).value(str("container")).toBool());
        QCOMPARE(d->containerInfo().value(str("members")).toInt(), 2);
        QVERIFY(host.action(str("diagramContainerFitAction"))->isEnabled());
        host.actions()->trigger(str("diagramContainerResizeFitAction"));
        QCOMPARE(d->containerInfo().value(str("resize")).toString(), str("fit"));
        QVERIFY(host.action(str("diagramContainerResizeFitAction"))->isChecked());
        host.actions()->trigger(str("diagramContainerSelectAction"));
        QCOMPARE(d->selection().size(), 2);
        d->setSelection({box});
        host.actions()->trigger(str("diagramContainerDisbandAction"));
        QVERIFY(d->item(box).isEmpty());
        QVERIFY(!d->item(a).isEmpty());
    }

    void layersLinksAndComments()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        const QString a = d->addShape(str("process"), 200, 200);
        const QString b = d->addShape(str("process"), 500, 200);
        QCOMPARE(d->addLayer(str("Draft")), 0);
        d->setSelection({b});
        QVERIFY(d->assignSelectionToLayers({0}));
        QCOMPARE(d->selectionLayers(), QVariantList{0});
        QVERIFY(d->setLayerProperty(0, str("visible"), false));
        QCOMPARE(d->layerList().first().toMap().value(str("count")).toInt(), 1);
        QindaOffice::Apps::Diagram::DiagramCanvasAdapter adapter;
        adapter.setService(d);
        QCOMPARE(adapter.canvasIds({a, b}).size(), 1); // the hidden shape has no proxy
        d->selectAll();
        QCOMPARE(d->selection(), QStringList{a});
        QVERIFY(!d->setLayerProperty(0, str("name"), QString())); // refused
        // A link to a page, followed.
        d->addPage();
        const QString details = d->currentPageId();
        d->renamePage({}, str("Details"));
        d->setCurrentPageId(d->pages().first().toMap().value(str("id")).toString());
        d->setSelection({a});
        QVERIFY(d->setSelectionLinks({QVariantMap{{str("subAddress"), str("Details")}, {str("description"), str("More")}}}));
        QVERIFY(host.action(str("diagramFollowLinkAction"))->isEnabled());
        host.actions()->trigger(str("diagramFollowLinkAction"));
        QCOMPARE(d->currentPageId(), details);
        // Comments: on the selection, a reply, a page comment; Next walks them.
        d->setCurrentPageId(d->pages().first().toMap().value(str("id")).toString());
        d->setSelection({a});
        const QString c1 = d->addComment(str("Is this step needed?"));
        QVERIFY(!d->addComment(str("Yes, for audits"), c1).isEmpty());
        d->setCurrentPageId(details);
        d->setSelection({});
        const QString c2 = d->addComment(str("Fill in later"));
        QCOMPARE(d->commentTotal(), 2);
        QCOMPARE(d->commentList().size(), 1);
        QCOMPARE(d->commentMarkers().size(), 1);
        host.actions()->trigger(str("diagramNextCommentAction"));
        QVERIFY(host.commentsPaneVisible());
        QCOMPARE(d->activeComment(), c1); // wraps to the first page
        QCOMPARE(d->commentList().first().toMap().value(str("replies")).toList().size(), 1);
        QCOMPARE(d->selection(), QStringList{a}); // its shape is selected
        QVERIFY(d->setCommentDone(c1, true));
        QVERIFY(d->commentList().first().toMap().value(str("done")).toBool());
        host.actions()->trigger(str("diagramDeleteCommentAction"));
        QCOMPARE(d->commentTotal(), 1);
        Q_UNUSED(c2)
    }

    void externalData()
    {
        QTemporaryDir dir;
        const QString csv = dir.filePath(str("steps.csv"));
        QVERIFY(writeFile(csv, "Step,Owner,Days\nReview application,Dana,2\nSchedule orientation,Sam,1\n"));
        DiagramHost host;
        DiagramService *d = host.diagram();
        const QString review = d->addShape(str("process"), 200, 200);
        d->setText(review, str("Review application"));
        const QString schedule = d->addShape(str("process"), 450, 200);
        d->setText(schedule, str("Schedule orientation"));
        const QVariantMap preview = d->readDataFile(QUrl::fromLocalFile(csv));
        QCOMPARE(preview.value(str("columns")).toStringList(), (QStringList{str("Step"), str("Owner"), str("Days")}));
        QCOMPARE(preview.value(str("rowCount")).toInt(), 2);
        QVERIFY(d->readDataFile(QUrl::fromLocalFile(dir.filePath(str("none.csv")))).contains(str("error")));
        int before = steps(host);
        const QString id = d->linkData(QUrl::fromLocalFile(csv), {}, 0, 0, {});
        QVERIFY(!id.isEmpty());
        QCOMPARE(steps(host), before + 1); // the recordset and its links: one step
        QCOMPARE(d->recordsetList().first().toMap().value(str("linked")).toInt(), 2);
        QCOMPARE(d->item(review).value(str("data")).toList().size(), 3);
        QVERIFY(d->item(review).value(str("dataLinked")).toBool());
        QCOMPARE(d->recordsetRows(id).at(1).toMap().value(str("linked")).toInt(), 1);
        // The file changes; Refresh All brings the new values.
        QVERIFY(writeFile(csv, "Step,Owner,Days\nReview application,Priya,3\nSchedule orientation,Sam,1\n"));
        host.actions()->trigger(str("diagramRefreshDataAction"));
        const QVariantList data = d->item(review).value(str("data")).toList();
        QCOMPARE(data.at(1).toMap().value(str("value")).toString(), str("Priya"));
        // Unlink keeps the values; shapes from rows are linked.
        d->setSelection({schedule});
        host.actions()->trigger(str("diagramUnlinkDataAction"));
        QVERIFY(!d->item(schedule).value(str("dataLinked")).toBool());
        const QStringList made = d->shapesFromRows(id, {0, 1}, str("process"), 1);
        QCOMPARE(made.size(), 2);
        QCOMPARE(d->item(made.first()).value(str("text")).toString(), str("Priya"));
        QVERIFY(d->removeRecordset(id));
        QVERIFY(d->recordsetList().isEmpty());
        QVERIFY(!d->item(made.first()).value(str("dataLinked")).toBool());
    }

    void autoConnectLayoutAndAutoSize()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        const QString a = d->addShape(str("process"), 200, 200);
        d->setSelection({a});
        QVERIFY(host.action(str("diagramConnectRightAction"))->isEnabled());
        int before = steps(host);
        host.actions()->trigger(str("diagramConnectRightAction"));
        QCOMPARE(steps(host), before + 1);
        const QString b = d->selection().value(0);
        QVERIFY(b != a);
        QCOMPARE(d->pageInfo().value(str("connectorCount")).toInt(), 1);
        d->setQuickShape(str("decision"));
        d->setSelection({b});
        host.actions()->trigger(str("diagramConnectDownAction"));
        QCOMPARE(d->item(d->selection().value(0)).value(str("type")).toString(), str("decision"));
        d->setSelection({});
        host.actions()->trigger(str("diagramLayoutRadialAction"));
        for (const auto &c : d->currentPage()->connectors) {
            QCOMPARE(c.routing, QindaOffice::Diagram::Routing::Straight); // radial draws them straight
        }
        QVERIFY(d->relayout({{str("kind"), str("circular")}, {str("rankGap"), 80}, {str("nodeGap"), 40}}));
        // Auto Size grows the page as a shape goes past its edge.
        host.actions()->trigger(str("diagramAutoSizeAction"));
        QVERIFY(host.action(str("diagramAutoSizeAction"))->isChecked());
        const double width = d->pageInfo().value(str("width")).toDouble();
        d->addShape(str("process"), width + 200, 200);
        QCOMPARE(d->pageInfo().value(str("width")).toDouble(), width * 2);
    }

    void printsAcrossSheetsWithItsBackground()
    {
        QTemporaryDir dir;
        DiagramHost host;
        DiagramService *d = host.diagram();
        d->addShape(str("process"), 200, 200);
        host.actions()->trigger(str("diagramBordersTitlesAction"));
        QCOMPARE(d->backgroundPageList().size(), 1);
        QCOMPARE(d->pageInfo().value(str("backPageId")).toString(), d->backgroundPageList().first().toMap().value(str("id")).toString());
        QVERIFY(d->setPrintSetup({{str("paper"), str("letter")}, {str("landscape"), true}, {str("zoom"), 0},
                                  {str("across"), 2}, {str("down"), 1}, {str("center"), true}}));
        QCOMPARE(d->pageBreaks().size(), 2);
        const QString path = dir.filePath(str("tiles.pdf"));
        QVERIFY(host.exportPdfTo(QUrl::fromLocalFile(path)));
        QFile pdf(path);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray bytes = pdf.readAll();
        // Two sheets for the page; the background page prints under it, not
        // on a sheet of its own.
        static const QRegularExpression page(QStringLiteral("/Type\\s*/Page[^s]"));
        QCOMPARE(QString::fromLatin1(bytes).count(page), 2);
    }
};

DIAGRAM_APP_TEST_MAIN(DiagramM87Test)

#include "tst_qindadiagram_m87.moc"
