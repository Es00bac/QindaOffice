// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// StrokeBuilder (plan section 7.4): turns raw tablet samples into smooth
// strokes - a light low-pass on pressure (tablet noise), minimum-distance
// resampling (hardware already reports at high rate; extra points only
// cost render time), and one-point linear interpolation when the pointer
// jumps (fast flicks) so curves stay continuous.

#include "stroke_model.h"

#include <QColor>

namespace QindaOffice::Ink {

class StrokeBuilder {
public:
    void begin(const StrokeSample &first, StrokeTool tool, const QColor &color, qreal width);
    void add(const StrokeSample &sample);   // filters + appends
    Stroke end();                            // marks complete and returns

    [[nodiscard]] const Stroke &current() const { return m_stroke; }
    [[nodiscard]] bool isActive() const { return m_active; }

    // Tuning (probe #6 informs the defaults).
    qreal minDistance = 1.5;    // px between kept samples
    qreal maxJump = 24.0;       // px; jumps beyond get an interpolated point
    qreal pressureSmoothing = 0.35; // low-pass factor for new samples

private:
    Stroke m_stroke;
    bool m_active = false;
    qreal m_lastPressure = 0.5;
};

} // namespace QindaOffice::Ink
