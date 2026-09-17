// SPDX-License-Identifier: GPL-3.0-or-later
#include "stroke_builder.h"
#include <QLineF>

namespace QindaOffice::Ink {

void StrokeBuilder::begin(const StrokeSample &first, StrokeTool tool, const QColor &color,
                          qreal width)
{
    m_stroke = Stroke{};
    m_stroke.tool = tool;
    m_stroke.color = color;
    m_stroke.width = width;
    m_stroke.complete = false;
    m_lastPressure = first.pressure;
    m_lastPos = first.pos;
    m_stroke.samples.append(first);
    m_active = true;
}

void StrokeBuilder::add(const StrokeSample &sample)
{
    if (!m_active || m_stroke.samples.isEmpty()) {
        return;
    }
    // Position EMA over the raw stream: every sample feeds the filter,
    // even the ones the min-distance test then drops.
    QPointF pos = sample.pos;
    if (positionSmoothing > 0.0) {
        pos = m_lastPos * (1.0 - positionSmoothing) + sample.pos * positionSmoothing;
    }
    m_lastPos = pos;
    const StrokeSample &last = m_stroke.samples.last();
    const qreal distance = QLineF(last.pos, pos).length();
    if (distance < minDistance) {
        return; // sub-pixel jitter
    }
    StrokeSample filtered = sample;
    filtered.pos = pos;
    // Low-pass the pressure channel (the noisiest on the Wacom One).
    filtered.pressure = m_lastPressure * (1.0 - pressureSmoothing)
        + sample.pressure * pressureSmoothing;
    m_lastPressure = filtered.pressure;
    if (distance > maxJump) {
        // Fast flick: insert one interpolated midpoint to keep curvature.
        StrokeSample mid;
        mid.pos = (last.pos + pos) * 0.5;
        mid.pressure = (last.pressure + filtered.pressure) * 0.5;
        m_stroke.samples.append(mid);
    }
    m_stroke.samples.append(filtered);
}

Stroke StrokeBuilder::end()
{
    m_stroke.complete = true;
    m_active = false;
    return m_stroke;
}

} // namespace QindaOffice::Ink
