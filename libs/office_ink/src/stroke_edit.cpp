// SPDX-License-Identifier: GPL-3.0-or-later
#include "stroke_edit.h"

#include <QLineF>

#include <algorithm>
#include <functional>

namespace QindaOffice::Ink {

namespace {

// Shortest distance from `pos` to the segment a→b (samples are sparse
// compared to the rendered line: testing samples only would leave the
// middle of a long segment unerasable).
[[nodiscard]] qreal segmentDistance(const QPointF &pos, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal length2 = QPointF::dotProduct(ab, ab);
    if (length2 < 1e-9) {
        return QLineF(a, pos).length();
    }
    const qreal t = qBound(0.0, qreal(QPointF::dotProduct(pos - a, ab)) / length2, 1.0);
    return QLineF(a + t * ab, pos).length();
}

// A sample counts as touched when the eraser disc reaches its rendered
// edge, not just its centre point — a 24-wide highlighter must erase when
// the disc crosses its visible body.
[[nodiscard]] bool touches(const Stroke &stroke, int sample, const QPointF &pos, qreal radius)
{
    return QLineF(stroke.samples.at(sample).pos, pos).length()
        <= radius + stroke.widthAt(sample) / 2.0;
}

// The disc touches the stroke's rendered body: any sample within reach,
// or any segment passing through the disc (using the wider of the two
// endpoint widths as the segment's reach).
[[nodiscard]] bool hitsStroke(const Stroke &stroke, const QPointF &pos, qreal radius)
{
    for (int i = 0; i < stroke.samples.size(); ++i) {
        if (touches(stroke, i, pos, radius)) {
            return true;
        }
        if (i > 0) {
            const qreal reach = radius
                + qMax(stroke.widthAt(i - 1), stroke.widthAt(i)) / 2.0;
            if (segmentDistance(pos, stroke.samples.at(i - 1).pos, stroke.samples.at(i).pos)
                <= reach) {
                return true;
            }
        }
    }
    return false;
}

// Splits `stroke` into the fragments that survive removing every sample
// the disc touches. Returns false when nothing was removed.
[[nodiscard]] bool partialErase(const Stroke &stroke, const QPointF &pos, qreal radius,
                                QVector<Stroke> *fragments)
{
    Stroke current;
    bool removed = false;
    const auto flush = [&current, fragments] {
        // A fragment shorter than two samples cannot render (the renderer
        // and the persistence both require two) — drop it rather than
        // persisting ink nothing can draw.
        if (current.samples.size() >= 2) {
            fragments->append(current);
        }
        current.samples.clear();
    };
    for (int i = 0; i < stroke.samples.size(); ++i) {
        if (touches(stroke, i, pos, radius)) {
            removed = true;
            flush();
            continue;
        }
        if (current.samples.isEmpty()) {
            current = stroke; // tool/color/width/complete carry over
            current.samples.clear();
        }
        current.samples.append(stroke.samples.at(i));
    }
    flush();
    return removed;
}

} // namespace

bool eraseAt(StrokeLayer *layer, const QSet<int> &candidates, const QPointF &pos,
             qreal radius, EraseMode mode)
{
    if (layer == nullptr || candidates.isEmpty()) {
        return false;
    }
    // Descending: a removal or replacement never disturbs the indices still
    // to be processed.
    QList<int> ordered = candidates.values();
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    bool changed = false;
    for (const int index : ordered) {
        const QVector<Stroke> &strokes = layer->strokes();
        if (index < 0 || index >= strokes.size()) {
            continue;
        }
        if (mode == EraseMode::Stroke) {
            if (hitsStroke(strokes.at(index), pos, radius)) {
                layer->removeIndices({index});
                changed = true;
            }
            continue;
        }
        QVector<Stroke> fragments;
        if (partialErase(strokes.at(index), pos, radius, &fragments)) {
            layer->replaceStroke(index, fragments);
            changed = true;
        }
    }
    return changed;
}

} // namespace QindaOffice::Ink
