// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sections of a deck (M75, D-084), as PowerPoint's Home ▸ Section menu
// works them: a section starts at a slide and runs to the next section;
// every slide is in exactly one once a deck has any; a section may be
// empty. The model keeps each section's slide ids (presentation.h); these
// operations keep that list true to the deck's order.
//
// AGENT-NOTE: after any change to the slide list the host calls
// normalizeSections(): a slide no section names (new, pasted, duplicated)
// joins the section of the slide before it (the first section at the top);
// a slide moved somewhere else joins the section it was dropped into
// (moveSlideInSections() first forgets where it was); names of slides that
// are gone are dropped; the order inside a section is the deck's.

#include "presentation.h"

namespace QindaOffice::Show {

void normalizeSections(Presentation &deck);
// Before a move: the slide leaves its section, to join the one it lands in.
void moveSlideInSections(Presentation &deck, const QString &slideId);

// The section a slide is in (index; -1 = none).
[[nodiscard]] int sectionOf(const Presentation &deck, const QString &slideId);

// Add Section: a new section starting at slide `slideId` (splitting the one
// it is in); a deck without sections gets "Default Section" for the slides
// before it. Returns the new section's index.
int addSection(Presentation &deck, const QString &slideId, const QString &name);
bool renameSection(Presentation &deck, int section, const QString &name);
// Remove Section: its slides join the section before it (the next one for
// the first); the last one standing takes the sections away altogether.
bool removeSection(Presentation &deck, int section);
void removeAllSections(Presentation &deck);
// Move Section Up / Down: the section and its slides as one block.
bool moveSection(Presentation &deck, int section, int delta);

} // namespace QindaOffice::Show
