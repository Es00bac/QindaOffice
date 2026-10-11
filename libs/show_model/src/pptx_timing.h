// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Slide transitions and builds between PowerPoint and QindaShow's player
// (D-070): the reader maps p:transition onto cut / fade / slide (push) /
// zoom — and since M67 (D-076) wipe and split — and the main sequence of
// p:timing onto per-shape build steps with their entrance effects and the
// Spin and Grow/Shrink emphasis effects; the writer keeps the original XML
// while those still match and writes PowerPoint's presets for them when
// the user changed them.
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
// M91 (D-099): Advance Slide — p:transition's @advTm (-1 when absent) and
// @advClick, read from the transition (or the mc:AlternateContent around
// it); applyAdvance() writes them into every p:transition `transition`
// holds, making a bare <p:transition/> when a slide without an effect has
// a timing (or does not advance on a click).
void readAdvance(const Ooxml::XmlElement *transition, int *advanceAfterMs, bool *advanceOnClick);
void applyAdvance(Ooxml::XmlElementPtr &transition, int advanceAfterMs, bool advanceOnClick);

// The entrance builds of the main sequence, one step per click (M91: and
// each shape's exit, BuildEntry::exit).
[[nodiscard]] QList<BuildEntry> readBuilds(const Ooxml::XmlElement *timing);
// How long the model plays a build as read: its longest behaviour within
// 50..5000 ms, 350 ms when it names none (the reader and the writer's
// "still what was read" check agree through this).
[[nodiscard]] inline int buildDuration(const BuildEntry &build)
{
    return build.durationMs > 0 ? qBound(50, build.durationMs, 5000) : 350;
}
// A p:timing playing `builds` and `media` (M75: each media shape's play
// command in the main sequence — at its click, or with the slide — and its
// media node); nullptr when there is nothing to play.
[[nodiscard]] Ooxml::XmlElementPtr timingElement(const QList<BuildEntry> &builds, const QList<MediaEntry> &media = {});
// M75 (pptx_timing_media.cpp): the media playback of `timing` — media
// nodes (p:video / p:audio) and the play commands of the main sequence.
[[nodiscard]] QList<MediaEntry> readMedia(const Ooxml::XmlElement *timing);
// The click step of each p:par of the main sequence's list (`clicks`), as
// both readers number them (M75): a group that starts with the slide and
// holds no entrance or emphasis effect (an automatic media play) is not a
// click (0); every other group is the next click.
[[nodiscard]] QList<int> clickSteps(const Ooxml::XmlElement &clicks);
// True when the main sequence group `par` starts with the slide (onBegin).
[[nodiscard]] bool startsWithSlide(const Ooxml::XmlElement &par);
// A copy of `timing` without anything aimed at a shape id outside
// `present`; nullptr when nothing playable is left.
[[nodiscard]] Ooxml::XmlElementPtr pruneTiming(const Ooxml::XmlElement &timing, const QSet<quint32> &present);

} // namespace QindaOffice::Show::Pptx
