// SPDX-License-Identifier: GPL-3.0-or-later
// Custom outlines and glyph strokes (shape_path.h).
#include "shape_path.h"

#include "stencils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

QString number(double value)
{
    // Seven significant digits: a millionth of the shape, far below a pixel.
    return QString::number(value, 'g', 7);
}

// AGENT-NOTE: glyphStrokes() learns what paintGlyph() draws by painting it
// onto this recorder instead of re-describing every stencil's glyph a second
// time: the painter hands the engine untransformed primitives plus its
// state, and the engine keeps each stroked path mapped to local space.
class StrokeRecorder final : public QPaintEngine {
public:
    StrokeRecorder()
        : QPaintEngine(QPaintEngine::AllFeatures)
    {
    }

    bool begin(QPaintDevice *) override { return true; }
    bool end() override { return true; }
    void updateState(const QPaintEngineState &state) override
    {
        if (state.state() & QPaintEngine::DirtyTransform) {
            m_transform = state.transform();
        }
        if (state.state() & QPaintEngine::DirtyPen) {
            m_pen = state.pen();
        }
    }
    void drawPath(const QPainterPath &path) override { record(path); }
    void drawPolygon(const QPointF *points, int count, PolygonDrawMode mode) override
    {
        if (count < 2) {
            return;
        }
        QPainterPath path(points[0]);
        for (int i = 1; i < count; ++i) {
            path.lineTo(points[i]);
        }
        if (mode != PolylineMode) {
            path.closeSubpath();
        }
        record(path);
    }
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {}
    void drawImage(const QRectF &, const QImage &, const QRectF &, Qt::ImageConversionFlags) override {}
    void drawTextItem(const QPointF &, const QTextItem &) override {}
    [[nodiscard]] Type type() const override { return QPaintEngine::User; }

    QList<QPainterPath> strokes;

private:
    void record(const QPainterPath &path)
    {
        if (m_pen.style() == Qt::NoPen || m_pen.color().alpha() == 0 || path.isEmpty()) {
            return;
        }
        strokes.append(m_transform.map(path));
    }

    QTransform m_transform;
    QPen m_pen;
};

class StrokeDevice final : public QPaintDevice {
public:
    [[nodiscard]] QPaintEngine *paintEngine() const override { return &engine; }
    mutable StrokeRecorder engine;

protected:
    [[nodiscard]] int metric(PaintDeviceMetric metric) const override
    {
        switch (metric) {
        case PdmWidth:
        case PdmHeight:
            return 100000;
        case PdmWidthMM:
        case PdmHeightMM:
            return 26458;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return 96;
        case PdmNumColors:
            return 0x7fffffff;
        case PdmDepth:
            return 32;
        case PdmDevicePixelRatio:
            return 1;
        case PdmDevicePixelRatioScaled:
            return int(QPaintDevice::devicePixelRatioFScale());
        default:
            return 0;
        }
    }
};

} // namespace

QString pathToText(const QPainterPath &path)
{
    QStringList parts;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            parts << QStringLiteral("M") << number(e.x) << number(e.y);
        } else if (e.isLineTo()) {
            parts << QStringLiteral("L") << number(e.x) << number(e.y);
        } else if (e.isCurveTo() && i + 2 < path.elementCount()) {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            parts << QStringLiteral("C") << number(e.x) << number(e.y) << number(c2.x) << number(c2.y)
                  << number(end.x) << number(end.y);
            i += 2;
        }
    }
    return parts.join(QLatin1Char(' '));
}

QPainterPath pathFromText(const QString &text)
{
    const QStringList tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QPainterPath path;
    qsizetype i = 0;
    const auto numbers = [&](int count, double *out) {
        if (i + count > tokens.size()) {
            return false;
        }
        for (int k = 0; k < count; ++k) {
            bool ok = false;
            out[k] = tokens.at(i + k).toDouble(&ok);
            if (!ok || !std::isfinite(out[k])) {
                return false;
            }
        }
        i += count;
        return true;
    };
    while (i < tokens.size()) {
        const QString command = tokens.at(i++);
        double v[6];
        if (command == QLatin1String("M") && numbers(2, v)) {
            path.moveTo(v[0], v[1]);
        } else if (command == QLatin1String("L") && numbers(2, v)) {
            path.lineTo(v[0], v[1]);
        } else if (command == QLatin1String("C") && numbers(6, v)) {
            path.cubicTo(v[0], v[1], v[2], v[3], v[4], v[5]);
        } else {
            return {}; // not ours: drop the path rather than guess (see header)
        }
    }
    return path;
}

QPainterPath scaledPath(const QPainterPath &normalized, QSizeF size)
{
    return QTransform::fromScale(std::max(1.0, size.width()), std::max(1.0, size.height())).map(normalized);
}

QPainterPath customOutline(const Shape &shape)
{
    QPainterPath all;
    all.setFillRule(Qt::WindingFill);
    for (const ShapePath &p : shape.paths) {
        all.addPath(scaledPath(p.path, shape.rect.size()));
    }
    return all;
}

bool customOutlineFills(const Shape &shape)
{
    for (const ShapePath &p : shape.paths) {
        if (p.fill && p.path.elementCount() > 2) {
            return true;
        }
    }
    return false;
}

// M65 (D-073): a custom outline, text box and connection points, written
// only when present so drawings of stencils keep their exact JSON.
void customToJson(const Shape &s, QJsonObject &known)
{
    if (!s.paths.isEmpty()) {
        QJsonArray paths;
        for (const ShapePath &p : s.paths) {
            paths.append(QJsonObject{{QStringLiteral("d"), pathToText(p.path)},
                                     {QStringLiteral("fill"), p.fill},
                                     {QStringLiteral("stroke"), p.stroke}});
        }
        known.insert(QStringLiteral("paths"), paths);
    }
    if (!s.textBox.isEmpty()) {
        known.insert(QStringLiteral("textBox"), QJsonArray{s.textBox.x(), s.textBox.y(), s.textBox.width(),
                                                           s.textBox.height()});
    }
    if (!s.ports.isEmpty()) {
        QJsonArray ports;
        for (const QPointF &p : s.ports) {
            ports.append(QJsonArray{p.x(), p.y()});
        }
        known.insert(QStringLiteral("ports"), ports);
    }
}

void customFromJson(const QJsonObject &o, Shape &s)
{
    for (const QJsonValue &v : o.value(QLatin1String("paths")).toArray()) {
        const QJsonObject p = v.toObject();
        const QPainterPath path = pathFromText(p.value(QLatin1String("d")).toString());
        if (!path.isEmpty()) {
            s.paths.append({path, p.value(QLatin1String("fill")).toBool(true), p.value(QLatin1String("stroke")).toBool(true)});
        }
    }
    const QJsonArray box = o.value(QLatin1String("textBox")).toArray();
    if (box.size() == 4) {
        s.textBox = QRectF(box.at(0).toDouble(), box.at(1).toDouble(), box.at(2).toDouble(), box.at(3).toDouble());
    }
    for (const QJsonValue &v : o.value(QLatin1String("ports")).toArray()) {
        const QJsonArray p = v.toArray();
        if (p.size() == 2) {
            s.ports.append(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
        }
    }
}

QList<QPainterPath> glyphStrokes(const Shape &shape)
{
    if (!shape.paths.isEmpty() || shape.type == QLatin1String("picture")) {
        return {};
    }
    StrokeDevice device;
    {
        QPainter painter(&device);
        paintGlyph(&painter, shape, QColor(Qt::black));
    }
    return device.engine.strokes;
}

} // namespace QindaOffice::Diagram
