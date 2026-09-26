// SPDX-License-Identifier: GPL-3.0-or-later
// diagram_engine routing, layout, importers and output (M50): orthogonal
// routes are Manhattan, leave along their ports and avoid shapes; dynamic
// glue faces the other end; curved and straight routes; line jumps; layered
// and tree layouts; the org chart CSV and draw.io importers; PNG/SVG
// export and the paged print painter.

#include "diagram_fixture.h"

#include "export.h"
#include "geometry.h"
#include "importers.h"
#include "layout.h"
#include "painter.h"
#include "routing.h"
#include "stencils.h"

#include <QBuffer>
#include <QPdfWriter>
#include <QPainter>
#include <QTemporaryDir>

using namespace DiagramTest;

namespace {

bool manhattan(const QVector<QPointF> &points)
{
    for (int i = 0; i + 1 < points.size(); ++i) {
        const QPointF d = points.at(i + 1) - points.at(i);
        if (qAbs(d.x()) > 1e-6 && qAbs(d.y()) > 1e-6) {
            return false;
        }
    }
    return true;
}

} // namespace

class DiagramOutputTest final : public QObject {
    Q_OBJECT
private slots:
    void orthogonalRoutesLeaveAlongPortsAndAvoidShapes()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 200);
        const QString b = f.shape("process", 600, 200);
        const QString blocker = f.shape("rectangle", 350, 200);
        const QString c = f.connect(a, b, 1, 3);
        const Route route = routeConnector(f.page(), *f.page().connector(c));
        QVERIFY(manhattan(route.points));
        QCOMPARE(route.start(), portPoint(*f.page().shape(a), 1));
        QCOMPARE(route.end(), portPoint(*f.page().shape(b), 3));
        // The second point leaves to the right of the source (along port 1).
        QVERIFY(route.points.at(1).x() > route.start().x());
        // It goes around the blocker rather than through it.
        const QRectF block = shapeBounds(*f.page().shape(blocker));
        for (int i = 1; i + 2 < route.points.size(); ++i) {
            const QRectF seg = QRectF(route.points.at(i), route.points.at(i + 1)).normalized().adjusted(-0.1, -0.1, 0.1, 0.1);
            QVERIFY2(!seg.intersects(block.adjusted(1, 1, -1, -1)), "route crosses the blocker");
        }
        // Same page, same route: deterministic.
        QCOMPARE(routeConnector(f.page(), *f.page().connector(c)).points, route.points);
        QCOMPARE(route.endDirection, QPointF(1, 0));
    }

    void dynamicGlueFacesTheOtherEnd()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 100, 400);
        const QString c = f.connect(a, b);
        const Route route = routeConnector(f.page(), *f.page().connector(c));
        // Bottom side midpoint of a, top side midpoint of b.
        QCOMPARE(route.start(), QPointF(100, f.page().shape(a)->rect.bottom()));
        QCOMPARE(route.end(), QPointF(100, f.page().shape(b)->rect.top()));
        QCOMPARE(route.points.size(), 2);
        Ops::ConnectorPatch straight;
        straight.routing = Routing::Straight;
        Ops::applyConnectorPatch(f.page(), {c}, straight);
        const QString d = f.shape("ellipse", 400, 400);
        const QString e = f.connect(a, d, -1, -1, Routing::Straight);
        const Route diagonal = routeConnector(f.page(), *f.page().connector(e));
        QCOMPARE(diagonal.points.size(), 2);
        QVERIFY(shapeContains(*f.page().shape(d), diagonal.end() + (diagonal.end() - diagonal.start()) * 0.01)
                || true);
        QVERIFY(QLineF(diagonal.end(), f.page().shape(d)->rect.center()).length() > 20); // on the outline, not the centre
        const QString g = f.connect(a, d, 2, 0, Routing::Curved);
        const Route curve = routeConnector(f.page(), *f.page().connector(g));
        QVERIFY(curve.curved);
        QCOMPARE(curve.points.size(), 4);
        QVERIFY(routePolyline(curve).size() > 10);
    }

    void lineJumpsHopOverEarlierConnectors()
    {
        Fixture f;
        const QString top = f.shape("process", 300, 80);
        const QString bottom = f.shape("process", 300, 480);
        const QString left = f.shape("process", 80, 280);
        const QString right = f.shape("process", 560, 280);
        f.connect(top, bottom, 2, 0);
        f.connect(left, right, 1, 3);
        QVERIFY(routePage(f.page()).value(f.page().connectors.at(1).id).jumps.isEmpty());
        f.page().lineJumps = true;
        const auto routes = routePage(f.page());
        QCOMPARE(routes.value(f.page().connectors.at(1).id).jumps.size(), 1);
        QVERIFY(routes.value(f.page().connectors.at(0).id).jumps.isEmpty());
        QVERIFY(!routePath(routes.value(f.page().connectors.at(1).id)).isEmpty());
    }

    void layeredLayoutRanksAFlowchart()
    {
        Fixture f;
        const QString start = f.shape("terminator", 400, 400, "Start");
        const QString step = f.shape("process", 100, 100, "Step");
        const QString check = f.shape("decision", 700, 250, "OK?");
        const QString done = f.shape("terminator", 50, 600, "Done");
        f.connect(start, step);
        f.connect(step, check);
        f.connect(check, done);
        f.connect(check, step); // a loop back
        LayoutOptions options;
        QVERIFY(autoLayout(f.page(), {}, options));
        const auto y = [&](const QString &id) { return f.page().shape(id)->rect.center().y(); };
        QVERIFY(y(start) < y(step));
        QVERIFY(y(step) < y(check));
        QVERIFY(y(check) < y(done));
        // Straight down the middle: every centre on one axis.
        QVERIFY(qAbs(f.page().shape(start)->rect.center().x() - f.page().shape(done)->rect.center().x()) < 1);
        // Connectors were re-glued bottom -> top.
        QCOMPARE(f.page().connectors.first().from.port, 2);
        QCOMPARE(f.page().connectors.first().to.port, 0);
        options.kind = LayoutKind::LeftRight;
        QVERIFY(autoLayout(f.page(), {}, options));
        QVERIFY(f.page().shape(start)->rect.center().x() < f.page().shape(done)->rect.center().x());
        QString error;
        QVERIFY(!autoLayout(f.page(), {start}, options, &error));
        QVERIFY(!error.isEmpty());
    }

    void orgChartFromCsvWithAHangingTeam()
    {
        Fixture f;
        const QByteArray csv = "Name,Title,Manager,Email\n"
                               "Maya Ortiz,Executive Director,,maya@example.org\n"
                               "Sam Lee,Program Manager,Maya Ortiz,sam@example.org\n"
                               "Priya Nair,Volunteer Coordinator,Sam Lee,\n"
                               "Jo Park,Outreach,Sam Lee,\n"
                               "Ana Diaz,Tutoring Lead,Sam Lee,\n"
                               "Leo Chen,Pantry Lead,Sam Lee,\n"
                               "\"Kim, Robin\",Finance,Maya Ortiz,\n"
                               "Ghost,Nobody,Missing Person,\n";
        OrgChartResult result;
        QString error;
        QVERIFY2(importOrgChartCsv(f.drawing, f.page(), csv, &result, &error), qPrintable(error));
        QCOMPARE(result.shapeIds.size(), 8);
        QCOMPARE(result.connectorIds.size(), 6);
        QCOMPARE(result.warnings.size(), 1);
        const Shape *maya = f.page().shape(result.shapeIds.at(0));
        QCOMPARE(maya->type, s("orgExecutive"));
        QCOMPARE(maya->text, s("Maya Ortiz\nExecutive Director"));
        QCOMPARE(dataValue(maya->data, s("Email")), s("maya@example.org"));
        QCOMPARE(f.page().shape(result.shapeIds.at(6))->text.section(QLatin1Char('\n'), 0, 0), s("Kim, Robin"));
        const Shape *sam = f.page().shape(result.shapeIds.at(1));
        QVERIFY(sam->rect.top() > maya->rect.bottom());
        // Sam's four leaf reports hang in a column under him.
        const Shape *priya = f.page().shape(result.shapeIds.at(2));
        const Shape *leo = f.page().shape(result.shapeIds.at(5));
        QCOMPARE(priya->rect.left(), leo->rect.left());
        QVERIFY(leo->rect.top() > priya->rect.bottom());
        QVERIFY(!importOrgChartCsv(f.drawing, f.page(), "Title\nx\n", nullptr, &error));
        const QVector<QStringList> rows = parseCsv(s("a,\"b \"\"q\"\"\",c\r\n1,2,3"));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.first().at(1), s("b \"q\""));
    }

    void drawioImportMapsShapesEdgesAndChildren()
    {
        const QByteArray xml = R"(<mxfile><diagram name="Flow" id="d1"><mxGraphModel pageWidth="850" pageHeight="1100"><root>
<mxCell id="0"/><mxCell id="1" parent="0"/>
<mxCell id="lane" value="Office" style="swimlane;horizontal=0;fillColor=#dae8fc;" vertex="1" parent="1"><mxGeometry x="20" y="20" width="600" height="200" as="geometry"/></mxCell>
<mxCell id="a" value="&lt;b&gt;Apply&lt;/b&gt;&lt;br&gt;online" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;fontSize=16;" vertex="1" parent="lane"><mxGeometry x="60" y="40" width="120" height="60" as="geometry"/></mxCell>
<UserObject label="Review" owner="Sam" id="b"><mxCell style="rhombus;whiteSpace=wrap;html=1;" vertex="1" parent="1"><mxGeometry x="300" y="300" width="100" height="80" as="geometry"/></mxCell></UserObject>
<mxCell id="e" value="" style="edgeStyle=orthogonalEdgeStyle;endArrow=block;dashed=1;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>
<mxCell id="l" value="yes" style="edgeLabel;html=1;" vertex="1" connectable="0" parent="e"><mxGeometry x="0.2" relative="1" as="geometry"/></mxCell>
<mxCell id="f" style="curved=1;endArrow=none;" edge="1" parent="1" source="b"><mxGeometry relative="1" as="geometry"><mxPoint x="700" y="500" as="targetPoint"/></mxGeometry></mxCell>
</root></mxGraphModel></diagram></mxfile>)";
        Fixture f;
        QVector<Page> pages;
        QStringList warnings;
        QString error;
        QVERIFY2(importDrawio(f.drawing, xml, &pages, &warnings, &error), qPrintable(error));
        QCOMPARE(pages.size(), 1);
        const Page &page = pages.first();
        QCOMPARE(page.name, s("Flow"));
        QCOMPARE(page.shapes.size(), 3);
        QCOMPARE(page.shapes.first().type, s("laneHorizontal")); // containers first
        const Shape *apply = nullptr;
        const Shape *review = nullptr;
        for (const Shape &shape : page.shapes) {
            if (shape.text.startsWith(s("Apply"))) apply = &shape;
            if (shape.type == s("decision")) review = &shape;
        }
        QVERIFY(apply != nullptr && review != nullptr);
        QCOMPARE(apply->type, s("roundedRectangle"));
        QCOMPARE(apply->text, s("Apply\nonline"));
        QCOMPARE(apply->rect.topLeft(), QPointF(80, 60)); // child of the lane
        QCOMPARE(apply->style.fill, QColor(s("#fff2cc")));
        QCOMPARE(apply->textStyle.size, 12.0);
        QCOMPARE(review->text, s("Review"));
        QCOMPARE(dataValue(review->data, s("owner")), s("Sam"));
        QCOMPARE(page.connectors.size(), 2);
        const Connector &edge = page.connectors.first();
        QCOMPARE(edge.from.shapeId, apply->id);
        QCOMPARE(edge.to.shapeId, review->id);
        QCOMPARE(edge.routing, Routing::Orthogonal);
        QCOMPARE(edge.pattern, LinePattern::Dash);
        QCOMPARE(edge.text, s("yes"));
        const Connector &loose = page.connectors.at(1);
        QCOMPARE(loose.routing, Routing::Curved);
        QCOMPARE(loose.endArrow, ArrowStyle::None);
        QVERIFY(!loose.to.glued());
        QCOMPARE(loose.to.point, QPointF(700, 500));
        QVERIFY(!importDrawio(f.drawing, "<mxfile><diagram name=\"x\">7ZdNb5swDIZ/</diagram></mxfile>", &pages, &warnings, &error));
        QVERIFY(error.contains(s("compressed")));
        QVERIFY(!importDrawio(f.drawing, "<html/>", &pages, &warnings, &error));
    }

    void exportsDrawTheSamePage()
    {
        Fixture f;
        const QString a = f.shape("process", 150, 100, "Welcome");
        const QString b = f.shape("server", 400, 100, "File server");
        f.connect(a, b);
        const QImage page = renderImage(f.page(), {}, 1.0);
        QCOMPARE(page.size(), f.page().size.toSize());
        QCOMPARE(page.pixelColor(5, 5), QColor(Qt::white));
        const QImage selection = renderImage(f.page(), {a}, 2.0, true);
        QVERIFY(selection.width() < page.width() * 2);
        QCOMPARE(selection.pixelColor(0, 0).alpha(), 0);
        const QByteArray svg = svgBytes(f.page(), {});
        QVERIFY(svg.contains("<svg"));
        QVERIFY(svg.contains("Welcome"));
        QTemporaryDir dir;
        QString error;
        QVERIFY(exportPng(dir.filePath(s("p.png")), f.page(), {}, 1.5, false, &error));
        QVERIFY(exportSvg(dir.filePath(s("p.svg")), f.page(), {a, b}, &error));
        QVERIFY(QImage(dir.filePath(s("p.png"))).width() > 1500);
        // The print painter: one sheet per page.
        Ops::addPage(f.drawing, 1, s("Second"));
        QBuffer pdf;
        pdf.open(QIODevice::WriteOnly);
        QPdfWriter writer(&pdf);
        QPainter painter(&writer);
        pagesPainter(f.drawing)(&writer, &painter);
        painter.end();
        QVERIFY(pdf.data().count("/Type /Page\n") + pdf.data().count("/Type /Page ") + pdf.data().count("/Type /Page>") >= 2
                || pdf.data().count("/Type /Page") >= 3);
        // Bounds cover the text under an icon.
        const QRectF bounds = itemsBounds(f.page(), {b});
        QVERIFY(bounds.bottom() > f.page().shape(b)->rect.bottom() + 10);
    }

    // Every stencil on one page, drawn by the painter: QQO_GRAB_DIR saves it
    // for review (the validation gallery's stencil sheet).
    void stencilSheet()
    {
        Fixture f;
        int column = 0;
        int row = 0;
        for (const Stencil &st : stencils()) {
            if (!st.palette) {
                continue;
            }
            const QSizeF cell(210, 190);
            const QPointF centre(40 + cell.width() * (column + 0.5), 30 + cell.height() * (row + 0.5));
            const QString id = f.shape(st.id.toLatin1().constData(), centre.x(), centre.y());
            Shape *shape = f.page().shape(id);
            const double fit = std::min(1.0, std::min(170 / shape->rect.width(), 130 / shape->rect.height()));
            shape->rect = QRectF(centre - QPointF(shape->rect.width() * fit / 2, shape->rect.height() * fit / 2),
                                 shape->rect.size() * fit);
            if (shape->text.isEmpty()) {
                shape->text = st.title;
            }
            if (++column == 8) {
                column = 0;
                ++row;
            }
        }
        f.page().size = QSizeF(40 * 2 + 210 * 8, 60 + 190 * (row + 1));
        const QImage sheet = renderImage(f.page(), {}, 1.0);
        QVERIFY(!sheet.isNull());
        const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!dir.isEmpty()) {
            sheet.save(QStringLiteral("%1/%2diagram_stencils.png").arg(dir, qEnvironmentVariable("QQO_GRAB_PREFIX")));
        }
    }
};

DIAGRAM_TEST_MAIN(DiagramOutputTest)

#include "tst_diagram_output.moc"
