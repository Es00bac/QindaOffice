// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PowerPoint's slide commands on the deck's own layouts (M82, D-091): New
// Slide on one of the deck's layouts, Layout, Reset Slide, Duplicate Slide,
// Reuse Slides (slides of another .pptx, keeping their formatting or taking
// this deck's) and Browse for Themes with a template's masters and layouts.
//
// AGENT-NOTE: like the design changes of pptx_design.h these are made where
// PowerPoint keeps them, not in the model's EFFECTIVE values: the deck is
// written (writePptxPackage), the change is made in the written package — a
// slide part whose placeholders carry no position or look of their own and
// are related to the chosen layout, a slide moved onto another layout, a
// template's master parts copied in — and the deck is read back
// (readBackEdited). Every placeholder then inherits from its layout exactly
// as PowerPoint will show it, and keeps inheriting: a later change to the
// layout moves it. Model ids of what QindaShow made since the last save are
// renewed (pptx_design.h); the new slide's id comes back to the caller.
//
// A deck without layouts (QindaShow's own, before it is saved as .pptx or
// opened in the Slide Master view) has nothing to choose from: the app keeps
// its six built-in slide kinds there.
//
// Threading: pure functions over values. Every function returns `deck`
// itself, with `error` set, when the change cannot be made.

#include "presentation.h"

#include <memory>

namespace QindaOffice::Ooxml {
class Package;
}

namespace QindaOffice::Show {

// One layout the New Slide gallery and the Layout menu offer, in
// PowerPoint's order: each master, then its layouts.
struct LayoutChoice {
    QString masterId;
    QString masterName;
    QString layoutId;
    QString name; // the layout's own name ("Title and Content")
    QString type; // ST_SlideLayoutType
    bool operator==(const LayoutChoice &) const = default;
};
[[nodiscard]] QList<LayoutChoice> layoutChoices(const Presentation &deck);
// The layout New Slide uses by default after `slide`, as PowerPoint picks
// it: after a title slide the master's Title and Content layout, else the
// slide's own layout ("" when the deck has none).
[[nodiscard]] QString nextSlideLayout(const Presentation &deck, const Slide *slide);

// New Slide: an empty slide on layout `layoutId` at deck position `index`
// (the end when out of range) — the layout's placeholders, empty, the date,
// footer and slide number only when the slide before shows them.
[[nodiscard]] Presentation withNewSlide(const Presentation &deck, const QString &layoutId, int index,
                                        QString *newSlideId, QString *error = nullptr);

// Layout: slide `slideId` moved onto layout `layoutId`. Its placeholders
// are matched to the new layout's (index, then type, then any content
// placeholder) and keep their text; unmatched ones stay where they were;
// the new layout's other placeholders join it, empty.
[[nodiscard]] Presentation withSlideLayout(const Presentation &deck, const QString &slideId, const QString &layoutId,
                                           QString *error = nullptr);

// Reset Slide: the slide's placeholders back to their layout's position,
// size and formatting (their text kept), the ones deleted from it back,
// empty.
[[nodiscard]] Presentation withResetSlide(const Presentation &deck, const QString &slideId, QString *error = nullptr);

// Duplicate Slide: a copy right after slide `slideId` — its own part on the
// same layout, a fresh creation id, its notes; its comments stay with the
// original.
[[nodiscard]] Presentation withDuplicatedSlide(const Presentation &deck, const QString &slideId,
                                               QString *newSlideId, QString *error = nullptr);

// Reuse Slides: slides `slides` (positions in `source`) inserted at `index`.
// keepSourceFormatting brings each slide's master, layouts and theme along
// (once per master); otherwise a slide takes this deck's layout of the same
// name, else of the same type, else its first content layout, and this
// deck's theme.
struct ReuseOptions {
    QList<int> slides;
    int index = -1;
    bool keepSourceFormatting = false;
};
[[nodiscard]] Presentation withReusedSlides(const Presentation &deck, const std::shared_ptr<const Ooxml::Package> &source,
                                            const ReuseOptions &options, QStringList *newSlideIds,
                                            QString *error = nullptr);

// Browse for Themes with a template (.potx / .pptx): its masters, layouts
// and themes join the deck, and slides `slideIds` (every slide when empty:
// Apply to All) move onto the template's layout of the same name, else type,
// else its first content layout. Masters no slide uses any more leave
// unless preserved — PowerPoint's Apply to All.
[[nodiscard]] Presentation withTemplateDesign(const Presentation &deck, const std::shared_ptr<const Ooxml::Package> &source,
                                              const QStringList &slideIds, QString *error = nullptr);

// Whether a package brings slide masters (a template or deck) rather than
// only a theme (.thmx).
[[nodiscard]] bool packageHasMasters(const Ooxml::Package &package);

} // namespace QindaOffice::Show
