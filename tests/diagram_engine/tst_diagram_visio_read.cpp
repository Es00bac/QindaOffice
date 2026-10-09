// SPDX-License-Identifier: GPL-3.0-or-later
// Visio import (M65, D-073): hand-built .vsdx packages read into the model —
// master and style inheritance, groups (rotated, mirrored, nested),
// connectors with their <Connects> glue and formula-only glue, text with
// runs, text blocks and fields, every Geometry row kind, pictures (cropped,
// and metafiles kept as frames), several pages with scale and a background
// page, theme colours, legacy formats refused.

#include "geometry.h"
#include "visio_fixture.h"
#include "visio_geometry.h"
#include "visio_io.h"
#include "visio_sheet.h"

#include <QBuffer>
#include <QImage>
#include <QtTest>

using namespace QindaOffice::Diagram;
using VisioFixture::rect;
using VisioFixture::str;
using VisioFixture::xform;

namespace {

Visio::ReadResult readPages(const QList<VisioFixture::PageSpec> &pages, const QList<VisioFixture::MasterSpec> &masters = {},
                            const QByteArray &theme = {})
{
    Visio::ReadResult result;
    QString error;
    const bool ok = Visio::readData(VisioFixture::build(pages, masters, {}, theme), &result, &error);
    if (!ok) {
        qWarning("read failed: %s", qPrintable(error));
    }
    return result;
}

VisioFixture::PageSpec page(const QString &shapes, const QString &connects = {})
{
    VisioFixture::PageSpec p;
    p.name = str("Page-1");
    p.contents = str("<Shapes>") + shapes + str("</Shapes>") + (connects.isEmpty() ? QString() : connects);
    return p;
}

Visio::Sheet sheetOf(const QString &shapeXml)
{
    const auto root = QindaOffice::Ooxml::parseXml(
        (str("<Shape xmlns='http://schemas.microsoft.com/office/visio/2012/main' xml:space='preserve'>") + shapeXml
         + str("</Shape>"))
            .toUtf8());
    return Visio::Sheet::merge(*root, nullptr);
}

bool near(QPointF a, QPointF b, double tolerance = 0.5)
{
    return QLineF(a, b).length() <= tolerance;
}

// Every vertex of a page outline (curves included as their end points).
QList<QPointF> vertices(const QPainterPath &path)
{
    QList<QPointF> out;
    for (int i = 0; i < path.elementCount(); ++i) {
        out.append(QPointF(path.elementAt(i).x, path.elementAt(i).y));
    }
    return out;
}

bool hasVertex(const QPainterPath &path, QPointF p)
{
    const QList<QPointF> all = vertices(path);
    return std::any_of(all.cbegin(), all.cend(), [&](QPointF v) { return near(v, p); });
}

} // namespace

class DiagramVisioReadTest final : public QObject {
    Q_OBJECT
private slots:
    void mastersLendCellsRowsAndText()
    {
        VisioFixture::MasterSpec master;
        master.id = 1;
        master.name = str("Box");
        master.contents =
            str("<Shapes><Shape ID='5' Type='Shape' LineStyle='3' FillStyle='3' TextStyle='3'>") + xform(1, 0.5, 2, 1)
            + str("<Cell N='FillForegnd' V='#5b9bd5'/><Cell N='LineColor' V='#41719c'/>")
            + str("<Section N='Property'><Row N='Cost'><Cell N='Value' V='12'/><Cell N='Label' V='Cost'/></Row>"
                  "<Row N='Hidden'><Cell N='Value' V='x'/><Cell N='Invisible' V='1'/></Row></Section>"
                  "<Section N='Connection'><Row T='Connection' IX='0'><Cell N='X' V='1'/><Cell N='Y' V='0'/></Row>"
                  "<Row T='Connection' IX='1'><Cell N='X' V='2'/><Cell N='Y' V='0.5'/></Row></Section>")
            + rect(2, 1) + str("<Text>Hello\n</Text></Shape></Shapes>");
        const Visio::ReadResult r = readPages(
            {page(str("<Shape ID='1' Type='Shape' Master='1'><Cell N='PinX' V='3'/><Cell N='PinY' V='4'/>"
                      "<Cell N='LineColor' V='#00ff00'/></Shape>"))},
            {master});
        QCOMPARE(r.drawing.pages.size(), 1);
        const Page &p = r.drawing.pages.first();
        QCOMPARE(p.size, QSizeF(1056, 816));
        QCOMPARE(p.shapes.size(), 1);
        const Shape &s = p.shapes.first();
        QCOMPARE(s.type, str("path"));
        QVERIFY(near(s.rect.topLeft(), QPointF(192, 384)));
        QCOMPARE(s.rect.size(), QSizeF(192, 96));
        QCOMPARE(s.style.fill, QColor(str("#5b9bd5")));
        QCOMPARE(s.style.line, QColor(str("#00ff00"))); // the instance's own cell
        QCOMPARE(s.text, str("Hello"));                   // the master's text, mark dropped
        QCOMPARE(s.data.size(), 1);                       // invisible rows stay in the file only
        QCOMPARE(s.data.first().key, str("Cost"));
        QCOMPARE(s.data.first().value, str("12"));
        QCOMPARE(s.ports.size(), 2);
        QVERIFY(near(s.ports.at(0), QPointF(0.5, 1.0), 1e-9));
        QVERIFY(near(s.ports.at(1), QPointF(1.0, 0.5), 1e-9));
        QCOMPARE(s.paths.size(), 1);
        QVERIFY(s.paths.first().fill);
        QVERIFY(s.extra.value(str("vsdx")).toObject().value(str("key")).toString() == str("0/1"));
    }

    void stylesInheritAlongTheirChains()
    {
        const Visio::ReadResult r = readPages({page(
            str("<Shape ID='1' Type='Shape' LineStyle='7' FillStyle='7' TextStyle='7'>") + xform(2, 2, 1, 1) + rect(1, 1)
            + str("<Text>T</Text></Shape>"
                  "<Shape ID='2' Type='Shape' LineStyle='0' FillStyle='0' TextStyle='0'>")
            + xform(4, 2, 1, 1, str("<Cell N='FillForegnd' V='24'/><Cell N='LinePattern' V='0'/>")) + rect(1, 1)
            + str("</Shape>"))});
        const Page &p = r.drawing.pages.first();
        QCOMPARE(p.shapes.size(), 2);
        const Shape &a = p.shapes.at(0);
        QCOMPARE(a.style.line, QColor(Qt::red));   // style 7 → its parent 3
        QCOMPARE(a.style.fill, QColor(Qt::blue));  // style 7 itself
        QCOMPARE(a.style.lineWidth, 1.0);          // No Style: 0.75 pt
        QCOMPARE(a.textStyle.family, str("Calibri"));
        QCOMPARE(a.textStyle.size, 12.0);
        QCOMPARE(a.textStyle.color, QColor(Qt::black)); // palette index 0
        const Shape &b = p.shapes.at(1);
        QCOMPARE(b.style.fill, QColor(str("#123456"))); // <Colors> entry 24
        QCOMPARE(b.style.line.alpha(), 0);               // LinePattern 0: no line
    }

    void groupsFlattenWithTheirTransforms()
    {
        const QString child = str("<Shape ID='%1' Type='Shape'>%2<Section N='Geometry' IX='0'>"
                                  "<Row T='RelMoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>"
                                  "<Row T='RelLineTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='0'/></Row>"
                                  "<Row T='RelLineTo' IX='3'><Cell N='X' V='1'/><Cell N='Y' V='1'/></Row>"
                                  "<Row T='RelLineTo' IX='4'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>"
                                  "</Section></Shape>");
        // A 2 × 2 in group turned a quarter counter-clockwise about (5, 4),
        // holding a nested group with one child.
        const QString group =
            str("<Shape ID='10' Type='Group'>") + xform(5, 4, 2, 2, str("<Cell N='Angle' V='1.570796326794897'/>"))
            + str("<Shapes>") + child.arg(11).arg(xform(0.5, 1, 1, 2))
            + str("<Shape ID='13' Type='Group'>") + xform(1.5, 1, 1, 2) + str("<Shapes>")
            + child.arg(12).arg(xform(0.5, 1, 1, 2)) + str("</Shapes></Shape></Shapes></Shape>");
        // A triangle mirrored top to bottom.
        const QString flipped = str("<Shape ID='20' Type='Shape'>") + xform(3, 3, 2, 1, str("<Cell N='FlipY' V='1'/>"))
                              + str("<Section N='Geometry' IX='0'><Row T='MoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>"
                                    "<Row T='LineTo' IX='2'><Cell N='X' V='2'/><Cell N='Y' V='0'/></Row>"
                                    "<Row T='LineTo' IX='3'><Cell N='X' V='0'/><Cell N='Y' V='1'/></Row>"
                                    "<Row T='LineTo' IX='4'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row></Section></Shape>");
        const Visio::ReadResult r = readPages({page(group + flipped)});
        const Page &p = r.drawing.pages.first();
        QCOMPARE(p.shapes.size(), 3);
        const Shape &a = p.shapes.at(0);
        const Shape &b = p.shapes.at(1);
        QVERIFY(!a.groupId.isEmpty());
        QCOMPARE(a.groupId, b.groupId); // nested groups: one model level
        QVERIFY(near(a.rect.center(), QPointF(480, 480)));
        QVERIFY(near(b.rect.center(), QPointF(480, 384)));
        QCOMPARE(a.rect.size(), QSizeF(96, 192));
        QVERIFY(std::abs(std::remainder(a.rotation + 90.0, 360.0)) < 1e-6);
        // Child A's local corner (0,0), through both frames: (6, 3) in.
        QVERIFY(hasVertex(pageOutline(a), QPointF(576, 528)));
        const Shape &t = p.shapes.at(2);
        QVERIFY(t.groupId.isEmpty());
        const QPainterPath outline = pageOutline(t);
        QVERIFY(hasVertex(outline, QPointF(192, 480)));
        QVERIFY(hasVertex(outline, QPointF(384, 480)));
        QVERIFY(hasVertex(outline, QPointF(192, 576)));
    }

    void connectorsGlueThroughConnectsAndFormulas()
    {
        const QString ports = str("<Section N='Connection'><Row T='Connection' IX='0'><Cell N='X' V='0'/><Cell N='Y' V='0.5'/></Row>"
                                  "<Row T='Connection' IX='1'><Cell N='X' V='0.5'/><Cell N='Y' V='0'/></Row></Section>");
        const QString shapes =
            str("<Shape ID='1' Type='Shape'>") + xform(2, 4, 1, 1) + rect(1, 1) + ports + str("</Shape>")
            + str("<Shape ID='2' Type='Shape'>") + xform(6, 4, 1, 1) + rect(1, 1) + ports + str("</Shape>")
            + str("<Shape ID='3' Type='Shape'>")
            + xform(4, 4, 3, 0.25,
                    str("<Cell N='BeginX' V='2.5'/><Cell N='BeginY' V='4'/><Cell N='EndX' V='5.5'/><Cell N='EndY' V='4'/>"
                        "<Cell N='EndArrow' V='4'/><Cell N='LineColor' V='#333333'/><Cell N='LineWeight' V='0.02083333333333333'/>"))
            + str("<Section N='Geometry' IX='0'><Cell N='NoFill' V='1'/>"
                  "<Row T='MoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0.125'/></Row>"
                  "<Row T='LineTo' IX='2'><Cell N='X' V='1.5'/><Cell N='Y' V='0.125'/></Row>"
                  "<Row T='LineTo' IX='3'><Cell N='X' V='3'/><Cell N='Y' V='0.125'/></Row></Section>"
                  "<Text>yes</Text></Shape>")
            // Glued by formula only (no Connect row): end 1 to shape 2's second point.
            + str("<Shape ID='4' Type='Shape'>")
            + xform(5, 2, 2, 0.25,
                    str("<Cell N='BeginX' V='6' F='PAR(PNT(Sheet.2!Connections.X2,Sheet.2!Connections.Y2))'/>"
                        "<Cell N='BeginY' V='3.5'/><Cell N='EndX' V='4'/><Cell N='EndY' V='2'/>"))
            + str("<Section N='Geometry' IX='0'><Cell N='NoFill' V='1'/><Row T='MoveTo' IX='1'><Cell N='X' V='0'/>"
                  "<Cell N='Y' V='0'/></Row><Row T='LineTo' IX='2'><Cell N='X' V='2'/><Cell N='Y' V='0'/></Row></Section></Shape>")
            // A filled 1-D shape (a block arrow) stays a shape.
            + str("<Shape ID='5' Type='Shape'>")
            + xform(8, 1, 2, 0.5, str("<Cell N='BeginX' V='7'/><Cell N='BeginY' V='1'/><Cell N='EndX' V='9'/><Cell N='EndY' V='1'/>"))
            + rect(2, 0.5) + str("</Shape>");
        const QString connects = str("<Connects><Connect FromSheet='3' FromCell='BeginX' FromPart='9' ToSheet='1' ToCell='PinX' "
                                     "ToPart='3'/><Connect FromSheet='3' FromCell='EndX' FromPart='12' ToSheet='2' "
                                     "ToCell='Connections.X1' ToPart='100'/></Connects>");
        const Visio::ReadResult r = readPages({page(shapes, connects)});
        const Page &p = r.drawing.pages.first();
        QCOMPARE(p.shapes.size(), 3);
        QCOMPARE(p.connectors.size(), 2);
        const Connector &c = p.connectors.at(0);
        QCOMPARE(c.from.shapeId, p.shapes.at(0).id);
        QCOMPARE(c.from.port, -1); // dynamic glue
        QCOMPARE(c.to.shapeId, p.shapes.at(1).id);
        QCOMPARE(c.to.port, 0);
        QVERIFY(near(c.from.point, QPointF(240, 432)));
        QCOMPARE(c.routing, Routing::Orthogonal);
        QCOMPARE(c.endArrow, ArrowStyle::Filled);
        QCOMPARE(c.startArrow, ArrowStyle::None);
        QCOMPARE(c.line, QColor(str("#333333")));
        QCOMPARE(c.lineWidth, 2.0);
        QCOMPARE(c.text, str("yes"));
        const Connector &f = p.connectors.at(1);
        QCOMPARE(f.from.shapeId, p.shapes.at(1).id);
        QCOMPARE(f.from.port, 1);
        QVERIFY(!f.to.glued());
        QCOMPARE(f.routing, Routing::Straight);
        QCOMPARE(p.shapes.at(2).type, str("path")); // the block arrow
    }

    void textKeepsRunsBlockAndFields()
    {
        const QString shape =
            str("<Shape ID='1' Type='Shape'>")
            + xform(3, 3, 2, 1,
                    str("<Cell N='VerticalAlign' V='0'/><Cell N='TxtPinX' V='1'/><Cell N='TxtPinY' V='0.25'/>"
                        "<Cell N='TxtWidth' V='1'/><Cell N='TxtHeight' V='0.5'/>"))
            + rect(2, 1)
            + str("<Section N='Character'><Row IX='0'><Cell N='Font' V='Arial'/><Cell N='Size' V='0.2222222222222222'/>"
                  "<Cell N='Style' V='1'/><Cell N='Color' V='#c00000'/></Row><Row IX='1'><Cell N='Style' V='2'/></Row></Section>"
                  "<Section N='Paragraph'><Row IX='0'><Cell N='HorzAlign' V='0'/></Row></Section>"
                  "<Text><cp IX='0'/><pp IX='0'/>Bold<cp IX='1'/> <cp IX='0'/>title <fld IX='0'>2026</fld>\n</Text></Shape>");
        const Visio::ReadResult r = readPages({page(shape)});
        const Shape &s = r.drawing.pages.first().shapes.first();
        // The space between two run markers is text (xml:space="preserve").
        QCOMPARE(s.text, str("Bold title 2026"));
        QCOMPARE(s.textStyle.family, str("Arial"));
        QCOMPARE(s.textStyle.size, 16.0);
        QVERIFY(s.textStyle.bold);
        QVERIFY(!s.textStyle.italic);
        QCOMPARE(s.textStyle.color, QColor(str("#c00000")));
        QCOMPARE(s.textStyle.align, Qt::AlignLeft | Qt::AlignTop);
        QVERIFY(std::abs(s.textBox.x() - 0.25) < 1e-9 && std::abs(s.textBox.y() - 0.5) < 1e-9);
        QVERIFY(std::abs(s.textBox.width() - 0.5) < 1e-9 && std::abs(s.textBox.height() - 0.5) < 1e-9);
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(str("several formats")));
    }

    void geometryRowsBecomePaths()
    {
        const auto figure = [](const QString &rows, double w = 4, double h = 2) {
            const QList<Visio::Figure> f =
                Visio::readGeometry(sheetOf(str("<Section N='Geometry' IX='0'>") + rows + str("</Section>")), w, h);
            return f.isEmpty() ? Visio::Figure() : f.first();
        };
        const QString start = str("<Row T='MoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>");
        // ArcTo with bow 1: a circle of radius 2.5 bulging below the chord.
        const Visio::Figure arc =
            figure(start + str("<Row T='ArcTo' IX='2'><Cell N='X' V='4'/><Cell N='Y' V='0'/><Cell N='A' V='1'/></Row>"));
        QVERIFY(std::abs(arc.path.boundingRect().top() - -1.0) < 0.02);
        QVERIFY(std::abs(arc.path.boundingRect().bottom()) < 0.02);
        QVERIFY(!arc.fill); // open
        // EllipticalArcTo through (2, 1) with a 2:1 axis ratio: the top at y 1.
        const Visio::Figure ellipticalArc = figure(
            start + str("<Row T='EllipticalArcTo' IX='2'><Cell N='X' V='4'/><Cell N='Y' V='0'/><Cell N='A' V='2'/>"
                        "<Cell N='B' V='1'/><Cell N='C' V='0'/><Cell N='D' V='2'/></Row>"));
        QVERIFY(std::abs(ellipticalArc.path.boundingRect().bottom() - 1.0) < 0.02);
        QVERIFY(ellipticalArc.path.pointAtPercent(0.5).x() > 1.9 && ellipticalArc.path.pointAtPercent(0.5).x() < 2.1);
        const Visio::Figure ellipse = figure(str("<Row T='Ellipse' IX='1'><Cell N='X' V='2'/><Cell N='Y' V='1'/><Cell N='A' V='4'/>"
                                                 "<Cell N='B' V='1'/><Cell N='C' V='2'/><Cell N='D' V='2'/></Row>"));
        QVERIFY(ellipse.fill);
        QVERIFY(std::abs(ellipse.path.boundingRect().width() - 4) < 0.02);
        QVERIFY(std::abs(ellipse.path.boundingRect().height() - 2) < 0.02);
        const Visio::Figure bezier = figure(
            str("<Row T='RelMoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>"
                "<Row T='RelCubBezTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='0'/><Cell N='A' V='0.25'/>"
                "<Cell N='B' V='1'/><Cell N='C' V='0.75'/><Cell N='D' V='1'/></Row>"
                "<Row T='RelQuadBezTo' IX='3'><Cell N='X' V='0'/><Cell N='Y' V='0'/><Cell N='A' V='0.5'/><Cell N='B' V='-1'/></Row>"));
        QVERIFY(bezier.fill); // back to its start: closed
        QVERIFY(std::abs(bezier.path.boundingRect().bottom() - 1.5) < 0.02); // the cubic's top
        QVERIFY(std::abs(bezier.path.boundingRect().top() - -1.0) < 0.02);   // the quadratic's bottom
        const Visio::Figure polyline = figure(
            start + str("<Row T='PolylineTo' IX='2'><Cell N='X' V='4'/><Cell N='Y' V='0'/>"
                        "<Cell N='A' V='POLYLINE(0, 0, 0.25, 0.5, 0.75, 0.5)'/></Row>"));
        QCOMPARE(vertices(polyline.path).size(), 4);
        QVERIFY(near(vertices(polyline.path).at(1), QPointF(1, 1), 1e-9));
        QVERIFY(near(vertices(polyline.path).at(2), QPointF(3, 1), 1e-9));
        // A clamped cubic NURBS is the Bézier through (0,0) (1,2) (3,2) (4,0).
        const Visio::Figure nurbs = figure(
            start + str("<Row T='NURBSTo' IX='2'><Cell N='X' V='4'/><Cell N='Y' V='0'/><Cell N='A' V='0'/><Cell N='B' V='1'/>"
                        "<Cell N='C' V='0'/><Cell N='D' V='1'/><Cell N='E' V='NURBS(1, 3, 1, 1, 1, 2, 0, 1, 3, 2, 0, 1)'/></Row>"));
        const QList<QPointF> sampled = vertices(nurbs.path);
        QVERIFY(sampled.size() > 10);
        QVERIFY(std::any_of(sampled.cbegin(), sampled.cend(), [](QPointF p) { return near(p, QPointF(2, 1.5), 0.05); }));
        QVERIFY(near(sampled.last(), QPointF(4, 0), 1e-9));
        const Visio::Figure spline = figure(
            start + str("<Row T='SplineStart' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='1'/><Cell N='A' V='0'/>"
                        "<Cell N='B' V='0'/><Cell N='C' V='1'/><Cell N='D' V='2'/></Row>"
                        "<Row T='SplineKnot' IX='3'><Cell N='X' V='4'/><Cell N='Y' V='0'/><Cell N='A' V='1'/></Row>"));
        QVERIFY(near(vertices(spline.path).last(), QPointF(4, 0), 1e-9));
        // NoShow sections draw nothing; NoFill / NoLine are kept per figure.
        const QList<Visio::Figure> flags = Visio::readGeometry(
            sheetOf(str("<Section N='Geometry' IX='0'><Cell N='NoShow' V='1'/>") + start
                    + str("<Row T='LineTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='1'/></Row></Section>"
                          "<Section N='Geometry' IX='1'><Cell N='NoFill' V='1'/><Cell N='NoLine' V='1'/>")
                    + start + str("<Row T='LineTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='1'/></Row></Section>")),
            1, 1);
        QCOMPARE(flags.size(), 1);
        QVERIFY(!flags.first().fill && !flags.first().stroke);
    }

    void picturesKeepTheirBytes()
    {
        QImage image(4, 2, QImage::Format_RGB32);
        image.fill(Qt::blue);
        for (int y = 0; y < 2; ++y) {
            image.setPixelColor(0, y, Qt::red);
            image.setPixelColor(1, y, Qt::red);
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        VisioFixture::PageSpec p = page(
            str("<Shape ID='1' Type='Foreign'>") + xform(2, 2, 2, 1)
            + str("<ForeignData ForeignType='Bitmap' CompressionType='PNG'><Rel r:id='rId9'/></ForeignData></Shape>"
                  "<Shape ID='2' Type='Foreign'>")
            + xform(5, 2, 1, 1, str("<Cell N='ImgOffsetX' V='0'/><Cell N='ImgOffsetY' V='0'/><Cell N='ImgWidth' V='2'/>"
                                    "<Cell N='ImgHeight' V='1'/>"))
            + str("<ForeignData ForeignType='Bitmap' CompressionType='PNG'><Rel r:id='rId9'/></ForeignData></Shape>"
                  "<Shape ID='3' Type='Foreign'>")
            + xform(7, 2, 1, 1) + str("<ForeignData ForeignType='EnhMetaFile'><Rel r:id='rId8'/></ForeignData></Shape>"));
        p.media = {{str("rId9"), png}, {str("rId8"), QByteArray("not a picture Qt reads")}};
        const Visio::ReadResult r = readPages({p});
        const Page &page1 = r.drawing.pages.first();
        QCOMPARE(page1.shapes.size(), 3);
        QCOMPARE(page1.shapes.at(0).type, str("picture"));
        QCOMPARE(page1.shapes.at(0).image, png);
        // Cropped: the shape shows the picture's left half (the red pixels).
        const QImage cropped = QImage::fromData(page1.shapes.at(1).image);
        QCOMPARE(cropped.size(), QSize(2, 2));
        QCOMPARE(cropped.pixelColor(1, 1), QColor(Qt::red));
        QVERIFY(page1.shapes.at(2).image.isEmpty()); // a frame stands in
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(str("EMF")));
    }

    void pagesKeepNamesSizesAndScale()
    {
        VisioFixture::PageSpec main = page(str("<Shape ID='1' Type='Shape'>") + xform(1, 1, 1, 1) + rect(1, 1) + str("</Shape>"));
        main.name = str("Main");
        main.width = 8.5;
        main.height = 11;
        main.backPage = 1;
        VisioFixture::PageSpec back = page(QString());
        back.name = str("Back");
        back.background = true;
        // 1 drawing inch = 0.5 paper inch: a 10 × 8 page is 5 × 4 in on paper.
        VisioFixture::PageSpec scaled = page(str("<Shape ID='1' Type='Shape'>") + xform(5, 4, 2, 2) + rect(2, 2) + str("</Shape>"));
        scaled.name = str("Scaled");
        scaled.width = 10;
        scaled.height = 8;
        scaled.sheetExtra = str("<Cell N='PageScale' V='1'/><Cell N='DrawingScale' V='2'/>");
        const Visio::ReadResult r = readPages({main, back, scaled});
        QCOMPARE(r.drawing.pages.size(), 3);
        QCOMPARE(r.drawing.pages.at(0).name, str("Main"));
        QCOMPARE(r.drawing.pages.at(0).size, QSizeF(816, 1056));
        QCOMPARE(r.drawing.pages.at(1).name, str("Back"));
        QCOMPARE(r.drawing.pages.at(2).size, QSizeF(480, 384));
        const Shape &s = r.drawing.pages.at(2).shapes.first();
        QVERIFY(near(s.rect.center(), QPointF(240, 192)));
        QCOMPARE(s.rect.size(), QSizeF(96, 96));
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(str("background page")));
    }

    void themedColoursComeFromTheTheme()
    {
        const QByteArray theme =
            "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>"
            "<a:theme xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' name='T'><a:themeElements>"
            "<a:clrScheme name='T'><a:dk1><a:srgbClr val='000000'/></a:dk1><a:lt1><a:srgbClr val='FFFFFF'/></a:lt1>"
            "<a:dk2><a:srgbClr val='44546A'/></a:dk2><a:lt2><a:srgbClr val='E7E6E6'/></a:lt2>"
            "<a:accent1><a:srgbClr val='4472C4'/></a:accent1><a:accent2><a:srgbClr val='70AD47'/></a:accent2>"
            "<a:accent3><a:srgbClr val='A5A5A5'/></a:accent3><a:accent4><a:srgbClr val='FFC000'/></a:accent4>"
            "<a:accent5><a:srgbClr val='5B9BD5'/></a:accent5><a:accent6><a:srgbClr val='ED7D31'/></a:accent6>"
            "<a:hlink><a:srgbClr val='0563C1'/></a:hlink><a:folHlink><a:srgbClr val='954F72'/></a:folHlink>"
            "<a:extLst><a:ext uri='{DDD2D869-C2EF-471E-B8FA-914AFA308C9F}'>"
            "<vt:variationClrSchemeLst xmlns:vt='http://schemas.microsoft.com/office/visio/2012/theme'>"
            "<vt:variationClrScheme><vt:varColor1><a:srgbClr val='1F3864'/></vt:varColor1></vt:variationClrScheme>"
            "</vt:variationClrSchemeLst></a:ext></a:extLst></a:clrScheme>"
            "<a:fontScheme name='T'><a:majorFont><a:latin typeface='Segoe UI'/></a:majorFont>"
            "<a:minorFont><a:latin typeface='Segoe UI'/></a:minorFont></a:fontScheme></a:themeElements></a:theme>";
        const QString themed = str("<Cell N='FillForegnd' V='Themed' F='THEMEVAL()'/><Cell N='FillPattern' V='1'/>");
        const Visio::ReadResult r = readPages(
            {page(str("<Shape ID='1' Type='Shape'>") + xform(2, 2, 1, 1, themed) + rect(1, 1)
                  + str("<Section N='Character'><Row IX='0'><Cell N='Color' V='Themed'/><Cell N='Font' V='Themed'/></Row>"
                        "</Section><Text>T</Text></Shape>")
                  + str("<Shape ID='2' Type='Shape'>")
                  + xform(4, 2, 1, 1, themed + str("<Cell N='QuickStyleFillColor' V='3'/>")) + rect(1, 1) + str("</Shape>"))},
            {}, theme);
        const Page &p = r.drawing.pages.first();
        QCOMPARE(p.shapes.at(0).style.fill, QColor(str("#1f3864"))); // variation colour 1
        QCOMPARE(p.shapes.at(0).textStyle.color, QColor(Qt::white)); // readable on it
        QCOMPARE(p.shapes.at(0).textStyle.family, str("Segoe UI"));  // the theme's body font
        QCOMPARE(p.shapes.at(1).style.fill, QColor(str("#70ad47"))); // index 3: accent2
    }

    void legacyFormatsAreRefusedClearly()
    {
        Visio::ReadResult result;
        QString error;
        QVERIFY(!Visio::read(str("/tmp/old drawing.vsd"), &result, &error));
        QVERIFY(error.contains(str("2003")));
        QVERIFY(error.contains(str(".vsdx")));
        QVERIFY(!Visio::readData(QByteArray("not a zip"), &result, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(DiagramVisioReadTest)
#include "tst_diagram_visio_read.moc"
