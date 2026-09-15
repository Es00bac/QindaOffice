// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Flat-ODP reader (plan section 7.5): parses exactly the subset the writer
// emits (frames, text boxes, custom shapes rect/ellipse, lines and arrows
// with markers, embedded base64 images, notes frames, qq: extensions) and
// tolerates the rest. Units convert to pt. Graphic styles are referenced
// by draw:style-name and resolved from the automatic-styles table.

#include "presentation.h"

namespace QindaOffice::Show {

[[nodiscard]] Presentation readFodp(const QByteArray &fodp, QString *error = nullptr);

} // namespace QindaOffice::Show
