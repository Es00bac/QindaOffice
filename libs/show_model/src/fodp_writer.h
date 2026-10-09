// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Flat-ODP writer (plan section 7.5): a valid ODF 1.3 presentation in one
// .fodp document — office:meta (title), the Arrow marker, automatic styles
// (fodp_styles.h: one graphic style per distinct shape look, P*/T* styles
// for paragraphs and runs, a list style for bullets, drawing-page styles
// for transitions and master backgrounds), one page layout, master pages,
// and draw:page slides with page-level draw:custom-shape / draw:line and
// framed text boxes and images, every draw:style-name / text:style-name
// linked. Lengths are written in points with their unit; rotation is the
// ODF rotate (counter-clockwise radians about the shape's origin) +
// translate pair. Builds, animations, groups and exact model values ride
// the qq: extension namespace; LibreOffice ignores them.

#include "presentation.h"

namespace QindaOffice::Show {

[[nodiscard]] QByteArray writeFodp(const Presentation &presentation, QString *error = nullptr);

} // namespace QindaOffice::Show
