// SPDX-License-Identifier: GPL-3.0-or-later
// M22 gates (plan "QindaNote UX overhaul" section 3.4): ink is filled
// geometry (outline widths, highlighter constant and UNDER the pen), and
// the eraser is a live model operation (whole-stroke and partial modes).

#include "ink_persistence.h"
#include "stroke_edit.h"
#include "stroke_geometry.h"
#include "stroke_model.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace QindaOffice::Ink;

class StrokeEditTest : public QObject {
    Q_OBJECT
private slots:
    void penOutlineWidthFollowsPressureWidth();
    void highlighterOutlineIgnoresPressure();
    void strokeEraserRemovesWholeStrokesAndReportsMisses();
    void partialEraserSplitsAStroke();
    void highlighterIsConstantWidthAndDrawnUnderPen();
};

void StrokeEditTest::penOutlineWidthFollowsPressureWidth()
{
    // A horizontal 2-sample pen stroke of width 4 at pressure 0.5 renders
    // 4 wide: widthAt() = 4 * (0.5 + 0.5), and round caps add nothing to
    // the height of a horizontal stroke.
    Stroke stroke;
    stroke.width = 4.0;
    stroke.samples = {{QPointF(0, 0), 0.5}, {QPointF(100, 0), 0.5}};
    const QRectF bounds = outline(stroke).boundingRect();
    QVERIFY(qAbs(bounds.height() - 4.0) < 0.6);
    // Round caps extend the outline past the samples by half the width.
    QVERIFY(bounds.left() < -1.5);
    QVERIFY(bounds.right() > 101.5);
}

void StrokeEditTest::highlighterOutlineIgnoresPressure()
{
    Stroke stroke;
    stroke.tool = StrokeTool::Highlighter;
    stroke.width = 12.0;
    stroke.samples = {{QPointF(0, 0), 0.2}, {QPointF(50, 0), 1.0}, {QPointF(100, 0), 0.2}};
    const QRectF bounds = outline(stroke).boundingRect();
    // Constant width end to end: no pressure term, flat caps.
    QVERIFY(qAbs(bounds.height() - 12.0) < 0.6);
    QVERIFY(qAbs(bounds.left() - 0.0) < 0.6);
    QVERIFY(qAbs(bounds.right() - 100.0) < 0.6);
}

void StrokeEditTest::strokeEraserRemovesWholeStrokesAndReportsMisses()
{
    StrokeLayer layer;
    Stroke a;
    a.samples = {{QPointF(10, 10), 0.5}, {QPointF(60, 10), 0.5}};
    Stroke b;
    b.samples = {{QPointF(300, 300), 0.5}, {QPointF(360, 300), 0.5}};
    layer.appendStroke(a);
    layer.appendStroke(b);

    // Candidates come from the spatial grid in the real path; a stroke
    // outside the candidate set is never touched even when it is in reach.
    QVERIFY(!eraseAt(&layer, {1}, QPointF(30, 10), 6.0, EraseMode::Stroke));
    QCOMPARE(layer.strokes().size(), 2);

    QVERIFY(eraseAt(&layer, {0, 1}, QPointF(30, 10), 6.0, EraseMode::Stroke));
    QCOMPARE(layer.strokes().size(), 1);
    QCOMPARE(layer.strokes().first().samples.first().pos, QPointF(300, 300));

    // Nothing left at that spot: the eraser reports "no change" instead of
    // pushing phantom undo steps (the old commit-time eraser pushed one per
    // gesture regardless).
    QVERIFY(!eraseAt(&layer, {0, 1}, QPointF(30, 10), 6.0, EraseMode::Stroke));
}

void StrokeEditTest::partialEraserSplitsAStroke()
{
    StrokeLayer layer;
    Stroke line;
    line.width = 2.0;
    for (int i = 0; i < 10; ++i) {
        line.samples.append({QPointF(i * 10.0, 0.0), 0.5});
    }
    line.complete = true;
    layer.appendStroke(line);

    // Erasing the middle out of a 10-sample line leaves the two ends as
    // two strokes, in the original slot.
    QVERIFY(eraseAt(&layer, {0}, QPointF(45, 0), 4.0, EraseMode::Partial));
    QCOMPARE(layer.strokes().size(), 2);
    QCOMPARE(layer.strokes().at(0).samples.size(), 4); // x = 0..30
    QCOMPARE(layer.strokes().at(1).samples.size(), 4); // x = 60..90
    QCOMPARE(layer.strokes().at(0).samples.constLast().pos, QPointF(30, 0));
    QCOMPARE(layer.strokes().at(1).samples.constFirst().pos, QPointF(60, 0));

    // Erasing an end down to a single sample drops that fragment: ink that
    // cannot render must not persist.
    QVERIFY(eraseAt(&layer, {0}, QPointF(0, 0), 22.0, EraseMode::Partial));
    QCOMPARE(layer.strokes().size(), 1);
    QCOMPARE(layer.strokes().first().samples.constFirst().pos, QPointF(60, 0));
}

void StrokeEditTest::highlighterIsConstantWidthAndDrawnUnderPen()
{
    QImage image(400, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);

    Stroke highlight;
    highlight.tool = StrokeTool::Highlighter;
    highlight.color = QColor(255, 210, 0);
    highlight.width = 12.0;
    // Pressure climbs along the stroke; the painted width must not.
    for (int i = 0; i <= 18; ++i) {
        highlight.samples.append({QPointF(20.0 + i * 20.0, 100.0), 0.1 + i * 0.05});
    }
    Stroke pen;
    pen.color = Qt::black;
    pen.width = 2.5;
    pen.samples = {{QPointF(200, 60), 0.8}, {QPointF(200, 140), 0.8}};
    pen.complete = true;
    highlight.complete = true;

    const QVector<Stroke> strokes{pen, highlight}; // pen FIRST in creation order
    QPainter painter(&image);
    renderStrokes(&painter, strokes);
    painter.end();

    // Where the pen crosses the highlighter, the pixel is the pen's — the
    // highlighter is one translucent fill UNDER the ink, not over it.
    const QColor crossing = image.pixelColor(200, 100);
    QVERIFY2(crossing.lightness() < 60, qPrintable(crossing.name()));

    // The highlighter paints as wide at its low-pressure end as at its
    // high-pressure end (width measured in unpainted columns off the pen).
    const auto paintedHeight = [&image](int x) {
        int top = -1;
        int bottom = -1;
        for (int y = 0; y < 200; ++y) {
            if (image.pixelColor(x, y).lightness() < 240) {
                if (top < 0) {
                    top = y;
                }
                bottom = y;
            }
        }
        return top < 0 ? 0 : bottom - top + 1;
    };
    const int nearStart = paintedHeight(30);  // pressure ~0.15
    const int nearEnd = paintedHeight(370);   // pressure ~1.0
    QVERIFY(qAbs(nearStart - nearEnd) <= 2);
    QVERIFY(nearStart >= 10 && nearStart <= 14);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    StrokeEditTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_stroke_edit.moc"
