// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Slide transitions and builds between PowerPoint and QindaShow's player
// (D-070): the reader maps p:transition onto cut/fade/slide/zoom and the
// main sequence of p:timing onto per-shape build steps and entrance
// effects; the writer keeps the original XML while those still match and
// writes the player's own spelling when the user changed them.
//
// AGENT-NOTE: p:timing names shapes by their Office id (p:spTgt/@spid).
// PowerPoint repairs (or refuses) a deck whose timing names a shape the
// slide does not have, so pruneTiming() drops the effects of deleted
// shapes before the original XML is written back.

#include "pptx_context.h"
#include "pptx_origin.h"

#include <QSet>

namespace QindaOffice::Show::Pptx {

// The player's name for a p:transition, or for the mc:AlternateContent
// PowerPoint wraps around newer ones.
[[nodiscard]] QString transitionName(const Ooxml::XmlElement *transition);
// A p:transition for one of the player's names; nullptr for "cut".
[[nodiscard]] Ooxml::XmlElementPtr transitionElement(const QString &name);

// The entrance builds of the main sequence, one step per click.
[[nodiscard]] QList<BuildEntry> readBuilds(const Ooxml::XmlElement *timing);
// A p:timing playing `builds` (empty list → nullptr).
[[nodiscard]] Ooxml::XmlElementPtr timingElement(const QList<BuildEntry> &builds);
// A copy of `timing` without anything aimed at a shape id outside
// `present`; nullptr when nothing playable is left.
[[nodiscard]] Ooxml::XmlElementPtr pruneTiming(const Ooxml::XmlElement &timing, const QSet<quint32> &present);

} // namespace QindaOffice::Show::Pptx
