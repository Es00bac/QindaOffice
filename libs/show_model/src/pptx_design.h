// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Deck-wide design changes (M75, D-084): PowerPoint's Design tab and the
// Slide Master view.
//
// AGENT-NOTE: the model holds EFFECTIVE values (D-070) — a slide's title
// is 44 pt because its master says so, a shape is blue because accent1
// is — so a change to what slides inherit (a theme, a master's text style
// or placeholder, a layout) cannot be applied to the slides one value at a
// time without guessing which values were inherited. Instead the change is
// made where PowerPoint keeps it (the master, layout or theme part — the
// pptx writer writes edited masters, layouts and themes, pptx_write_
// masters.h) and the deck is RE-RESOLVED: written and read back through
// the same reader that opened it (reresolved()), with no zip in between
// and the media bytes shared. Every slide then inherits exactly as
// PowerPoint will show it: a placeholder that never had its own position
// moves with its layout, a run that never chose a size follows the master,
// theme colours change and explicit ones do not. A deck QindaShow made
// comes back with PowerPoint's structure (a master with a theme, its
// layouts) — what saving it as .pptx gives it anyway.
//
// Model ids of what QindaShow made since the last save (slides, shapes)
// are renewed by a re-resolution; callers clear selections and keep the
// current slide by position. QindaShow's own grouping (Shape::groupId)
// and extension payloads (Shape::extra), which .pptx does not carry, are
// kept by position.
//
// Threading: pure functions over values.

#include "presentation.h"

namespace QindaOffice::Ooxml {
class PackageWriter;
}

namespace QindaOffice::Show {

// `deck` written and read back (see above); on failure `deck` itself and
// `error`.
[[nodiscard]] Presentation reresolved(const Presentation &deck, QString *error = nullptr);
// `deck` as `written` (its package, edited where PowerPoint keeps things —
// pptx_slides.h, M82) reads back. `fromDeck[i]` is the index in `deck` of
// read slide i, or -1 for a slide the edit made: the others keep
// QindaShow's own state by position. On failure `deck` itself and `error`.
[[nodiscard]] Presentation readBackEdited(const Presentation &deck, const Ooxml::PackageWriter &written,
                                          const QList<int> &fromDeck, QString *error = nullptr);

// ---- Slide Size ----
enum class SlideScale { EnsureFit, Maximize };
// PowerPoint asks Maximize or Ensure Fit when the shape of the slide
// changes and its content would have to shrink to fit; otherwise it fits.
[[nodiscard]] bool resizeAsks(const QSizeF &from, const QSizeF &to);
// The factor content is scaled by: Ensure Fit the smaller of the two
// ratios, Maximize the larger.
[[nodiscard]] qreal resizeFactor(const QSizeF &from, const QSizeF &to, SlideScale scale);
// The deck at `size`: every slide's, layout's and master's shapes scaled
// by resizeFactor() about the slide's top-left and centred, text sizes,
// spacing, indents, table grids and insets with them, written into the
// parts and re-resolved — a placeholder without a position of its own
// keeps following its layout.
[[nodiscard]] Presentation resized(const Presentation &deck, const QSizeF &size, SlideScale scale,
                                   QString *error = nullptr);

// ---- Themes ----
enum class ThemePart { Whole, Colors, Fonts, Effects };
// `theme` (or only its colours, fonts or effects) on master `masterId`
// (every master when empty), re-resolved.
[[nodiscard]] Presentation themed(const Presentation &deck, const ThemeSpec &theme, ThemePart part,
                                  const QString &masterId = QString(), QString *error = nullptr);
// The theme the deck shows for `masterId` (the first master when empty).
[[nodiscard]] ThemeSpec deckTheme(const Presentation &deck, const QString &masterId = QString());

// ---- Format Background ----
// `background` on slide `slideId` (unset = Reset Background: the layout's
// or master's again), or — `all`, Apply to All — on every master, with the
// layouts' and slides' own backgrounds cleared so every slide shows it.
[[nodiscard]] Presentation withBackground(const Presentation &deck, const QString &slideId, const Background &background,
                                          bool all);

} // namespace QindaOffice::Show
