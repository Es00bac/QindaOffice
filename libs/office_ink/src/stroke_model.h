// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stroke model (plan section 7.4): polylines sampled from pen input with
// per-sample pressure and tilt. Strokes are append-only while drawing; a
// stroke list per layer forms one ink layer. Serialization is compact
// JSON (the notebook files stay human-readable, plan 6.4).

#include <QColor>
#include <QPolygonF>
#include <QVector>

namespace QindaOffice::Ink {

struct StrokeSample {
    QPointF pos;
    qreal pressure = 0.5; // 0..1
    qreal tiltX = 0;      // degrees, unused by basic rendering
    qreal tiltY = 0;
};

enum class StrokeTool { Pen, Highlighter, Eraser };

struct Stroke {
    QVector<StrokeSample> samples;
    StrokeTool tool = StrokeTool::Pen;
    QColor color = Qt::black;
    qreal width = 2.5;      // base pen width in px at pressure 0.5
    bool complete = false;  // false while the pen is still down

    [[nodiscard]] QRectF bounds() const;
    // Width at one sample: pressure-scaled around the base width
    // (plan: pressure width), clamped so hovering (p~0) still renders.
    [[nodiscard]] qreal widthAt(int sample) const;
};

class StrokeLayer {
public:
    [[nodiscard]] const QVector<Stroke> &strokes() const { return m_strokes; }
    Stroke &appendStroke(const Stroke &stroke);
    void updateLast(const Stroke &stroke); // while drawing
    bool removeAt(const QPointF &pos, qreal tolerance);
    [[nodiscard]] const Stroke *strokeAt(const QPointF &pos, qreal tolerance) const;
    void clear();
    // Indexed surgery for stroke_edit (erase/split now, selection ops in
    // M24): indices follow strokes(). removeIndices keeps the survivors'
    // order; replaceStroke puts the fragments in the removed stroke's slot
    // (empty = plain removal), so z-order never shuffles under the eraser.
    void removeIndices(const QVector<int> &indices);
    void replaceStroke(int index, const QVector<Stroke> &fragments);

    [[nodiscard]] QRectF bounds() const;
    [[nodiscard]] QByteArray toJson() const;
    [[nodiscard]] bool fromJson(const QByteArray &json);

private:
    QVector<Stroke> m_strokes;
};

} // namespace QindaOffice::Ink
