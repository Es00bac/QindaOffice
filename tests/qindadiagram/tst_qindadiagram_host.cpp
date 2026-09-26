// SPDX-License-Identifier: GPL-3.0-or-later
// QindaDiagram's host and service without a window (M50): command
// inventory (ids, icons, labels, no shared shortcut), enablement and the
// ribbon contexts follow the selection; the tools are one radio group;
// pages; styles and the format painter; the canvas adapter's projection
// and one-step commits; copy/paste between two windows; org chart CSV and
// draw.io import through the service; PNG/SVG export and the print
// callback's PDF (one sheet per page); save/load through the document.

#include "diagram_app_test_support.h"

#include "qindadiagram/src/canvas_adapter.h"

#include "drawing_document.h"
#include "drawing_io.h"

#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QSet>
#include <QUndoStack>

using namespace DiagramAppTest;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace

class DiagramHostTest final : public QObject {
    Q_OBJECT
private slots:
    void commandInventory()
    {
        DiagramHost host;
        QSet<QString> shortcuts;
        int diagramCommands = 0;
        for (auto *entry : host.actions()->entries()) {
            QVERIFY2(!entry->text().isEmpty(), qPrintable(entry->id()));
            if (entry->id().startsWith(str("diagram"))) {
                ++diagramCommands;
                if (!entry->id().startsWith(str("diagramPaper")) && !entry->id().startsWith(str("diagramTheme_"))) {
                    QVERIFY2(!entry->iconName().isEmpty(), qPrintable(entry->id()));
                }
            }
            const QString keys = entry->shortcut().toString();
            if (!keys.isEmpty()) {
                QVERIFY2(!shortcuts.contains(keys), qPrintable(entry->id() + str(" shares ") + keys));
                shortcuts.insert(keys);
            }
        }
        QVERIFY2(diagramCommands > 100, qPrintable(QString::number(diagramCommands)));
        QCOMPARE(host.action(str("diagramDuplicateAction"))->shortcut(), QKeySequence(str("Ctrl+D")));
        QCOMPARE(host.action(str("fileNewAction"))->plainText(), str("New Drawing"));
    }

    void enablementContextsAndTools()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        QVERIFY(!host.action(str("diagramAlignLeftAction"))->isEnabled());
        QVERIFY(!host.action(str("editUndoAction"))->isEnabled());
        const QString a = d->addShape(str("process"), 200, 200);
        const QString b = d->addShape(str("process"), 400, 260);
        QVERIFY(host.action(str("editUndoAction"))->isEnabled());
        d->setSelection({a, b});
        QVERIFY(host.action(str("diagramAlignLeftAction"))->isEnabled());
        QVERIFY(!host.action(str("diagramDistributeHorizontalAction"))->isEnabled());
        QCOMPARE(host.ribbonContexts(), QStringList{str("shape")});
        host.actions()->trigger(str("diagramAlignTopAction"));
        QCOMPARE(d->item(a).value(str("y")).toDouble(), d->item(b).value(str("y")).toDouble());
        const QString c = link(d, a, 1, b, 3);
        QCOMPARE(host.ribbonContexts(), QStringList{str("connector")});
        host.actions()->trigger(str("diagramRouteCurvedAction"));
        QCOMPARE(d->item(c).value(str("routing")).toString(), str("curved"));
        QVERIFY(host.action(str("diagramRouteCurvedAction"))->isChecked());
        host.actions()->trigger(str("diagramArrowBothAction"));
        QCOMPARE(d->item(c).value(str("startArrow")).toString(), str("filled"));
        // Tools are one radio group mirrored by the service.
        host.actions()->trigger(str("diagramToolTextAction"));
        QCOMPARE(d->tool(), str("text"));
        QVERIFY(!host.action(str("diagramToolPointerAction"))->isChecked());
        d->setTool(str("pointer"));
        QVERIFY(host.action(str("diagramToolPointerAction"))->isChecked());
        // Group, then any member selects the whole group.
        d->setSelection({a, b});
        host.actions()->trigger(str("diagramGroupAction"));
        d->setSelection({a});
        QCOMPARE(d->selection().size(), 2);
        QVERIFY(host.action(str("diagramUngroupAction"))->isEnabled());
        host.actions()->trigger(str("editUndoAction"));
        d->setSelection({a});
        QCOMPARE(d->selection().size(), 1);
    }

    void pagesAndDesign()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        QCOMPARE(d->pages().size(), 1);
        host.actions()->trigger(str("diagramNewPageAction"));
        QCOMPARE(d->pages().size(), 2);
        QCOMPARE(d->currentPageIndex(), 1);
        QVERIFY(d->renamePage({}, str("Network")));
        QCOMPARE(d->pageInfo().value(str("name")).toString(), str("Network"));
        host.actions()->trigger(str("diagramPaperA4Action"));
        QCOMPARE(d->pageInfo().value(str("paper")).toString(), str("a4"));
        QVERIFY(d->pageInfo().value(str("landscape")).toBool()); // orientation kept
        host.actions()->trigger(str("diagramPortraitAction"));
        QVERIFY(!d->pageInfo().value(str("landscape")).toBool());
        QVERIFY(host.action(str("diagramPaperA4Action"))->isChecked());
        host.actions()->trigger(str("diagramTheme_forest"));
        QCOMPARE(d->themeId(), str("forest"));
        QVERIFY(!d->setPageSize(10, 10));
        QVERIFY(d->deletePage());
        QCOMPARE(d->pages().size(), 1);
        QVERIFY(!d->deletePage());
    }

    void stylesAndFormatPainter()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        const QString a = d->addShape(str("process"), 200, 200);
        const QString b = d->addShape(str("decision"), 400, 200);
        d->setSelection({a});
        QVERIFY(d->setStyle({{str("fill"), str("#ff0000")}, {str("shadow"), true}, {str("lineWidth"), 3}}));
        QVERIFY(d->setTextStyle({{str("bold"), true}, {str("size"), 14}, {str("align"), str("left")}}));
        QCOMPARE(d->selected().value(str("fill")).toString(), str("#ff0000"));
        QVERIFY(d->selected().value(str("textStyle")).toMap().value(str("bold")).toBool());
        host.actions()->trigger(str("formatGrowFontAction"));
        QCOMPARE(d->selected().value(str("textStyle")).toMap().value(str("size")).toDouble(), 15.0);
        d->setFormatPainter(true);
        QVERIFY(host.action(str("diagramFormatPainterAction"))->isChecked());
        QVERIFY(d->applyFormatTo({b}));
        QVERIFY(!d->formatPainter());
        QCOMPARE(d->item(b).value(str("fill")).toString(), str("#ff0000"));
        QVERIFY(d->item(b).value(str("shadow")).toBool());
        QVERIFY(d->setData(a, str("Owner"), str("Front desk")));
        QVERIFY(!d->setData(a, str(" "), str("x")));
        QCOMPARE(d->item(a).value(str("data")).toList().size(), 1);
        d->setSelection({});
        d->setFormatPainter(true); // nothing to copy from: refused
        QVERIFY(!d->formatPainter());
    }

    void adapterProjectsAndCommitsOneStep()
    {
        DiagramHost host;
        DiagramService *d = host.diagram();
        QindaOffice::Apps::Diagram::DiagramCanvasAdapter adapter;
        adapter.setService(d);
        const QString a = d->addShape(str("process"), 200, 200);
        const QString b = d->addShape(str("process"), 500, 200);
        const QString c = link(d, a, 1, b, 3);
        const QVariantList ids = adapter.canvasIds({a, b, c});
        QCOMPARE(ids.size(), 3);
        QCOMPARE(adapter.itemIds(ids), (QStringList{a, b, c}));
        const auto *shape = adapter.object(ids.at(0).toULongLong());
        QVERIFY(shape != nullptr);
        QCOMPARE(shape->shape, QindaTK::CanvasShape::Image);
        const auto *line = adapter.object(ids.at(2).toULongLong());
        QCOMPARE(line->shape, QindaTK::CanvasShape::Polyline);
        QVERIFY(line->points.size() >= 2);
        // A move of shape a: one undo step, and the connector follows.
        const int steps = host.drawingDocument()->undoStack()->count();
        const double x0 = d->item(a).value(str("x")).toDouble();
        QindaTK::CanvasEdit edit;
        QindaTK::CanvasObject after = *shape;
        after.transform = after.transform * QTransform::fromTranslate(30, 40);
        edit.changes.append({shape->id, *shape, after});
        QVERIFY(adapter.commit(edit));
        QCOMPARE(host.drawingDocument()->undoStack()->count(), steps + 1);
        QCOMPARE(d->item(a).value(str("x")).toDouble(), x0 + 30);
        // A resize through the transform's scale lands in rect + rotation.
        shape = adapter.object(ids.at(0).toULongLong());
        after = *shape;
        QTransform scale = shape->transform;
        scale.scale(2, 1);
        after.transform = scale;
        QindaTK::CanvasEdit resize;
        resize.changes.append({shape->id, *shape, after});
        QVERIFY(adapter.commit(resize));
        QCOMPARE(d->item(a).value(str("width")).toDouble(), 264.0);
        // A stale edit is refused.
        QVERIFY(!adapter.commit(edit));
    }

    void clipboardBetweenWindows()
    {
        DiagramHost one;
        DiagramHost two;
        DiagramService *a = one.diagram();
        const QString s1 = a->addShape(str("process"), 200, 200);
        const QString s2 = a->addShape(str("decision"), 400, 200);
        link(a, s1, 1, s2, 3);
        a->selectAll();
        QVERIFY(a->copy());
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());
        QTRY_VERIFY(two.action(str("editPasteAction"))->isEnabled());
        const QStringList pasted = two.diagram()->paste();
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(two.diagram()->pageInfo().value(str("connectorCount")).toInt(), 1);
        QVERIFY(two.diagram()->item(pasted.at(2)).value(str("fromGlued")).toBool());
        QVERIFY(a->copyAsImage());
        QVERIFY(!QGuiApplication::clipboard()->image().isNull());
    }

    void importsExportsAndPrinting()
    {
        QTemporaryDir dir;
        DiagramHost host;
        DiagramService *d = host.diagram();
        const QString csv = dir.filePath(str("people.csv"));
        QVERIFY(writeFile(csv, "Name,Title,Manager\nAda,Director,\nBo,Coordinator,Ada\nCy,Coordinator,Ada\n"));
        QVERIFY(d->importOrgChart(QUrl::fromLocalFile(csv)));
        QCOMPARE(d->pageInfo().value(str("shapeCount")).toInt(), 3);
        QCOMPARE(d->selection().size(), 3);
        const QString drawio = dir.filePath(str("flow.drawio"));
        QVERIFY(writeFile(drawio, "<mxfile><diagram name=\"Imported\"><mxGraphModel><root><mxCell id=\"0\"/>"
                                  "<mxCell id=\"1\" parent=\"0\"/><mxCell id=\"a\" value=\"A\" style=\"ellipse\" vertex=\"1\" "
                                  "parent=\"1\"><mxGeometry x=\"10\" y=\"10\" width=\"80\" height=\"40\" as=\"geometry\"/>"
                                  "</mxCell></root></mxGraphModel></diagram></mxfile>"));
        QVERIFY(d->importDrawio(QUrl::fromLocalFile(drawio)));
        QCOMPARE(d->pages().size(), 2); // the org chart page was not empty
        QCOMPARE(d->pageInfo().value(str("name")).toString(), str("Imported"));
        QVERIFY(d->exportPng(QUrl::fromLocalFile(dir.filePath(str("page.png"))), false));
        QVERIFY(!QImage(dir.filePath(str("page.png"))).isNull());
        QVERIFY(d->exportSvg(QUrl::fromLocalFile(dir.filePath(str("page.svg"))), false));
        QVERIFY(host.exportPdfTo(QUrl::fromLocalFile(dir.filePath(str("drawing.pdf")))));
        QFile pdf(dir.filePath(str("drawing.pdf")));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF"));
        QVERIFY(host.action(str("filePrintAction"))->isEnabled());
        // Save through the document, reopen in another window.
        QString error;
        const QString path = dir.filePath(str("saved.qdiagram"));
        QVERIFY2(host.drawingDocument()->save(path, &error), qPrintable(error));
        DiagramHost reopened;
        QVERIFY(reopened.loadPath(path));
        QCOMPARE(reopened.diagram()->pages().size(), 2);
        QVERIFY(reopened.drawingDocument()->drawing() == host.drawingDocument()->drawing());
    }
};

DIAGRAM_APP_TEST_MAIN(DiagramHostTest)

#include "tst_qindadiagram_host.moc"
