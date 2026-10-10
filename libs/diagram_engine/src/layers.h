// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Layers and background pages (M87, D-095): the rules every consumer of the
// model shares — the painter (what shows, in which colour), print and
// export (what goes on paper), the canvas (what can be picked and moved),
// glue (which shapes take connector ends) and the Visio writer.
//
// AGENT-CONTRACT (Visio's rules, docs/diagram.md "Layers"): an item on no
// layer always shows, prints and edits; an item on layers is hidden only
// when EVERY one of them is hidden, left off paper only when none of them
// prints, locked when ANY of them is locked, and glued to only when one of
// them takes glue. The first of its layers with a colour colours it.

#include "drawing.h"

#include <QColor>
#include <QStringList>
#include <QVector>

namespace QindaOffice::Diagram {

[[nodiscard]] bool layersVisible(const Page &page, const QList<int> &layers);
[[nodiscard]] bool layersPrint(const Page &page, const QList<int> &layers);
[[nodiscard]] bool layersLocked(const Page &page, const QList<int> &layers);
[[nodiscard]] bool layersGlue(const Page &page, const QList<int> &layers);
// The colour a layer gives its items, or an invalid colour.
[[nodiscard]] QColor layersColor(const Page &page, const QList<int> &layers);

// The same rules for an item of the page by id (false for an unknown id,
// except itemLocked, which is true).
[[nodiscard]] bool itemVisible(const Page &page, const QString &itemId);
[[nodiscard]] bool itemLocked(const Page &page, const QString &itemId);

// Indexes of the page's active layers (new items go on them).
[[nodiscard]] QList<int> activeLayers(const Page &page);

// The ids of the page's items that do not show (every layer hidden).
[[nodiscard]] QStringList hiddenItems(const Page &page);

// The background pages shown behind `page`, farthest first: its background
// page, that page's background page … A cycle (or a missing page) ends the
// chain; a page is never its own background.
[[nodiscard]] QVector<const Page *> backgroundChain(const Drawing &drawing, const Page &page);

} // namespace QindaOffice::Diagram
