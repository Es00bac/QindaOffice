// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pptx_write_shapes.h"
namespace QindaOffice::Show::Pptx {
// Recognizes a standalone Office equation. Mixed text/equation boxes remain
// preserved until the text model supports inline math runs.
bool readEquation(Shape &shape, const Ooxml::XmlElement &element);
[[nodiscard]] Ooxml::XmlElementPtr generateEquation(PartWriter &out, const Shape &shape, quint32 id);
// Patch native math inside a carried shape, preserving other attributes,
// extensions and relationships. Refresh only its raster compatibility view.
void patchEquation(PartWriter &out, Ooxml::XmlElement &element, const Shape &shape, quint32 id);
}
