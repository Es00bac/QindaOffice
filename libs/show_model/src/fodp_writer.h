// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Flat-ODP writer (plan section 7.5): the agreed subset of ODF presentation
// XML in one .fodp document - office:document with the presentation
// mimetype, automatic styles (one graphic style per shape, paragraph/text
// styles), one page layout in cm, a single Default master page, and
// draw:page slides containing draw:frame (text-box / image with
// office:binary-data), draw:custom-shape with enhanced geometry for
// rect/ellipse, draw:line with a marker for arrows, and a notes frame.
// Transitions ride the drawing-page style's smil:type; builds and other
// extensions live in the qq: namespace.

#include "presentation.h"

namespace QindaOffice::Show {

[[nodiscard]] QByteArray writeFodp(const Presentation &presentation, QString *error = nullptr);

} // namespace QindaOffice::Show
