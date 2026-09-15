// SPDX-License-Identifier: GPL-3.0-or-later
// office_ink gates (plan section 7.4): builder filters, pressure width,
// spatial queries, JSON round-trips, and the page envelope.

#include "ink_persistence.h"
#include "spatial_grid.h"
#include "stroke_builder.h"
#include "stroke_model.h"

#include <QCoreApplication>
#include <QPainter>
#include <QtTest>

using namespace QindaOffice::Ink;

class OfficeInkTest : public QObject {
    Q_OBJECT
private slots:
    void builderFiltersJitterAndInterpolatesJumps();
    void pressureScalesWidth();
    void spatialGridFindsNearbyStrokes();
    void layerJsonRoundTrips();
    void pageEnvelopeRoundTrips();
    void rendererPaintsStrokes();
};

void OfficeInkTest::builderFiltersJitterAndInterpolatesJumps()
{
    StrokeBuilder builder;
    builder.begin({QPointF(0, 0), 0.5}, StrokeTool::Pen, Qt::black, 2.0);
    builder.add({QPointF(0.4, 0.2), 0.5});   // below minDistance: dropped
    builder.add({QPointF(0.8, 0.4), 0.5});   // below: dropped
    builder.add({QPointF(10, 0), 0.5});
    builder.add({QPointF(80, 0), 0.5});      // jump: midpoint inserted
    const Stroke stroke = builder.end();
    QVERIFY(stroke.complete);
    QCOMPARE(stroke.samples.size(), 4); // first + kept + mid + jump end
    QCOMPARE(stroke.samples.at(2).pos.x(), 45.0);
}

void OfficeInkTest::pressureScalesWidth()
{
    Stroke stroke;
    stroke.width = 2.0;
    stroke.samples = {{QPointF(0, 0), 0.0}, {QPointF(10, 0), 1.0}};
    QVERIFY(stroke.widthAt(1) > stroke.widthAt(0));
    QCOMPARE(stroke.widthAt(0), 1.0); // 0.5 * 2.0
}

void OfficeInkTest::spatialGridFindsNearbyStrokes()
{
    Stroke a;
    a.samples = {{QPointF(10, 10), 0.5}, {QPointF(60, 10), 0.5}};
    Stroke b;
    b.samples = {{QPointF(500, 500), 0.5}, {QPointF(560, 500), 0.5}};
    SpatialGrid grid;
    grid.rebuild({a, b});
    QCOMPARE(grid.queryPoint(QPointF(30, 10)), 0);
    QCOMPARE(grid.queryPoint(QPointF(530, 500)), 1);
    QCOMPARE(grid.queryPoint(QPointF(300, 300)), -1);
    QVERIFY(grid.query(QRectF(0, 0, 100, 100)).contains(0));
    QVERIFY(!grid.query(QRectF(0, 0, 100, 100)).contains(1));
}

void OfficeInkTest::layerJsonRoundTrips()
{
    StrokeLayer layer;
    Stroke stroke;
    stroke.tool = StrokeTool::Highlighter;
    stroke.color = QColor(255, 200, 0, 200);
    stroke.width = 4.0;
    stroke.samples = {{QPointF(1, 2), 0.25, 5, -3}, {QPointF(30, 40), 0.9, 0, 0}};
    stroke.complete = true;
    layer.appendStroke(stroke);
    StrokeLayer back;
    QVERIFY(back.fromJson(layer.toJson()));
    QCOMPARE(back.strokes().size(), 1);
    QCOMPARE(back.strokes().first().tool, StrokeTool::Highlighter);
    QCOMPARE(back.strokes().first().samples.size(), 2);
    QCOMPARE(back.strokes().first().samples.first().tiltX, 5.0);
    QVERIFY(back.strokes().first().color.alpha() != 255);
}

void OfficeInkTest::pageEnvelopeRoundTrips()
{
    PageEnvelope page;
    page.id = QStringLiteral("p1");
    page.title = QStringLiteral("Ideas");
    page.background = QStringLiteral("grid");
    Stroke stroke;
    stroke.samples = {{QPointF(5, 5), 0.6}, {QPointF(50, 50), 0.6}};
    stroke.complete = true;
    page.ink.appendStroke(stroke);
    page.blocksJson = R"({"blocks":[{"type":"text","text":"hello"}]})";

    PageEnvelope back;
    QVERIFY(pageFromJson(pageToJson(page), &back));
    QCOMPARE(back.id, page.id);
    QCOMPARE(back.background, QStringLiteral("grid"));
    QCOMPARE(back.ink.strokes().size(), 1);
    QVERIFY(back.blocksJson.contains("hello"));
}

void OfficeInkTest::rendererPaintsStrokes()
{
    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    Stroke stroke;
    stroke.samples = {{QPointF(10, 10), 0.8}, {QPointF(150, 150), 0.8}};
    QPainter painter(&image);
    renderStroke(&painter, stroke);
    painter.end();
    bool painted = false;
    for (int y = 0; y < 200 && !painted; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (image.pixelColor(x, y).lightness() < 100) {
                painted = true;
                break;
            }
        }
    }
    QVERIFY(painted);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    OfficeInkTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_office_ink.moc"
