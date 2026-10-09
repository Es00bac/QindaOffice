// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native PowerPoint (.pptx) writer (M62, D-070, D-068). A deck read from
// .pptx keeps its masters, layouts, themes, presProps, viewProps,
// tableStyles, comment authors, embedded fonts and every preserved part
// (carried by Ooxml::RelationshipCopier with fresh relationship ids); its
// slides are written from the model, each shape verbatim while unchanged,
// patched where edited (pptx_write_shapes.h). A deck QindaShow made gets a
// generated set: a master per QindaShow master with title and body
// placeholders, the Title Slide / Title and Content / Blank layouts, a
// theme, and a notes master when there are notes.
//
// PowerPoint's own rules, beyond the schema: slide ids ≥ 256, master and
// layout ids ≥ 2147483648 and unique together, sldSz and notesSz present,
// every slide related to a layout, shape ids unique per slide, notes
// slides related to the notes master and back to their slide, and an
// Override in [Content_Types].xml for every part.

#include "presentation.h"

namespace QindaOffice::Show {

[[nodiscard]] QByteArray writePptx(const Presentation &deck, QString *error = nullptr);
bool writePptxFile(const Presentation &deck, const QString &path, QString *error = nullptr);

} // namespace QindaOffice::Show
