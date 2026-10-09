// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// a:txBody → effective paragraphs (pptx reader, internal; D-070). The
// caller passes what the body inherits (`inherited`: the presentation's
// default text style overlaid with the master's title/body/other style and
// the master's and layout's placeholder list styles) and the shape's
// p:style fontRef; the body's own a:lstStyle, the paragraph properties and
// the run properties go on top.

#include "pptx_context.h"

namespace QindaOffice::Show::Pptx {

[[nodiscard]] QList<TextParagraph> readTextBody(const PartContext &ctx, const Ooxml::XmlElement *txBody,
                                                const ListStyle &inherited, const RunProps &styleRun = {});

} // namespace QindaOffice::Show::Pptx
