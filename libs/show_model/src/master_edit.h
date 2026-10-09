// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Slide Master view's model operations (M75, D-084), as PowerPoint's
// Slide Master tab names them: Insert / Duplicate / Delete / Rename
// Layout, Preserve, Master Layout's Title and Footers, Insert Placeholder,
// and the master's text styles. Each changes the model only; the caller
// re-resolves the deck (pptx_design.h) so slides inherit the change, and
// pushes the before/after deck as one undo step.
//
// Layouts get ids "slideLayout<N>" (N above every layout id in the deck),
// which the pptx writer uses as their part names, so a layout keeps its
// id through every re-resolution and save.

#include "presentation.h"

namespace QindaOffice::Show {

// The layouts of `masterId`, in the deck's order.
[[nodiscard]] QList<const SlideLayout *> layoutsOfMaster(const Presentation &deck, const QString &masterId);
// How many slides use the layout.
[[nodiscard]] int slidesUsingLayout(const Presentation &deck, const QString &layoutId);

// Insert Layout: a custom layout of `masterId` with the master's title and
// footer placeholders, after `afterLayoutId` (last when empty); its id.
QString insertLayout(Presentation &deck, const QString &masterId, const QString &afterLayoutId);
// Duplicate Layout: a copy right after it, named "1_<name>"; its id.
QString duplicateLayout(Presentation &deck, const QString &layoutId);
// Delete Layout: false (nothing changed) while a slide uses it.
bool deleteLayout(Presentation &deck, const QString &layoutId);
bool renameLayout(Presentation &deck, const QString &layoutId, const QString &name);
bool renameMaster(Presentation &deck, const QString &masterId, const QString &name);
bool setLayoutPreserved(Presentation &deck, const QString &layoutId, bool preserve);

// Master Layout: whether the layout has a title / the date, footer and
// slide number placeholders, and switching them (from the master's).
[[nodiscard]] bool layoutHasTitle(const SlideLayout &layout);
[[nodiscard]] bool layoutHasFooters(const SlideLayout &layout);
bool setLayoutTitle(Presentation &deck, const QString &layoutId, bool on);
bool setLayoutFooters(Presentation &deck, const QString &layoutId, bool on);

// Insert Placeholder: "obj" (Content), "body" (Text) or "pic" (Picture) on
// a layout, at `rect`; its model id.
QString insertPlaceholder(Presentation &deck, const QString &layoutId, const QString &type, const QRectF &rect);

// ---- text styles ----
enum class StyleList { Title, Body, Other };
// The master's level (0..8) of a style list; nullptr when out of range.
[[nodiscard]] const TextParagraph *styleLevel(const SlideMaster &master, StyleList list, int level);
// Sets one level of a master text style (its paragraph look and run look,
// as `look` has them); false when unchanged.
bool setStyleLevel(Presentation &deck, const QString &masterId, StyleList list, int level, const TextParagraph &look);

// The master or a layout as a slide the editor can show and edit: its
// placeholders and decorations in z order (placeholders first), with the
// master's / layout's background. `layoutId` empty = the master itself.
[[nodiscard]] Slide templateSlide(const Presentation &deck, const QString &masterId, const QString &layoutId);
// Puts an edited template slide's shapes back (placeholders and
// decorations split again) and its background; false when unchanged.
bool applyTemplateSlide(Presentation &deck, const QString &masterId, const QString &layoutId, const Slide &edited);

} // namespace QindaOffice::Show
