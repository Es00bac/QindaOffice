// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Flat-ODP reader (plan section 7.5): page-level draw:custom-shape /
// draw:rect / draw:ellipse / draw:line / draw:connector, framed text boxes
// and embedded images, groups (draw:g → a QindaShow group), notes frames,
// and the qq: extensions. Every length carries its unit (a bare number is
// the pre-M62 spelling of cm, see odf_dom.h); styles resolve through
// fodp_read_styles.h (parent chains included), text through
// fodp_read_text.h. Reads what the writer emits exactly, LibreOffice's own
// flat ODP as far as the model reaches, and the pre-M62 QindaShow files
// (unlinked shape-<name> styles, fo: attributes on spans, svg:x/y plus a
// clockwise rotate()).

#include "presentation.h"

namespace QindaOffice::Show {

[[nodiscard]] Presentation readFodp(const QByteArray &fodp, QString *error = nullptr);

} // namespace QindaOffice::Show
