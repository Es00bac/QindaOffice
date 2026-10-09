// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One slide and its notes for the pptx writer (internal; D-070): the
// source p:sld kept around everything QindaShow rebuilds (custom data,
// controls, extensions, the colour-map override), the shape tree with ids
// kept stable, the background, the transition (the source's while the
// player's name for it is unchanged) and p:timing (the source's, pruned of
// deleted shapes, while the builds are unchanged; the player's builds
// otherwise).

#include "pptx_write_shapes.h"

namespace QindaOffice::Show::Pptx {

[[nodiscard]] Ooxml::XmlElementPtr slideElement(PartWriter &out, const Slide &slide);
// The p:notes for the slide; nullptr when it has no notes.
[[nodiscard]] Ooxml::XmlElementPtr notesElement(PartWriter &out, const Slide &slide);
// Relationships the slide XML never names (comments), carried across.
void carryImplicitRelationships(PartWriter &out, const Slide &slide);

} // namespace QindaOffice::Show::Pptx
