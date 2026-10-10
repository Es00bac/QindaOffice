// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one painter (M50, D-050): the screen canvas, the Shapes panel
// thumbnails, Print / Print Preview / Export PDF, PNG and SVG export and
// Copy as Image all draw a page through these functions, in page units on
// whatever device and transform the caller set up — so what is on screen is
// what prints (plan §5.2's one-callback rule, extended to every output).
//
// AGENT-GUARD: text is laid out at a fixed 100 px font and scaled into
// place, so line breaks are identical at every zoom and on every device
// (a QPdfWriter at 1200 dpi and a 96 dpi QImage wrap the same words).

#include "drawing.h"
#include "routing.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

class QPainter;

namespace QindaOffice::Diagram {

struct PaintOptions {
    bool background = true;     // fill the page with its background colour
    bool grid = false;          // the page grid (screen only; never printed)
    QColor gridColor = QColor(226, 232, 240);
    QSet<QString> only;         // non-empty: paint just these items
    QSet<QString> hideTextOf;   // items whose text an editor is covering
    // Precomputed routes (the canvas caches them); null: route here.
    const QHash<QString, Route> *routes = nullptr;
    // M87 (D-095): paper rules — items whose layers do not print stay off
    // (print, PDF and the exports; the screen shows them).
    bool printing = false;
    // M87: background pages painted under this one, farthest first
    // (layers.h backgroundChain); never the screen grid.
    QVector<const Page *> backgrounds;
};

void paintPage(QPainter *painter, const Page &page, const PaintOptions &options = {});
void paintShape(QPainter *painter, const Shape &shape, bool withText = true);
void paintConnector(QPainter *painter, const Connector &connector, const Route &route,
                    const QColor &labelBackground, bool withText = true);
// Arrowhead of `style` with its tip at `tip`, pointing along `direction`.
void paintArrow(QPainter *painter, ArrowStyle style, QPointF tip, QPointF direction, const QColor &color,
                double lineWidth);
// How far an arrowhead's base sits behind its tip (the line stops there).
[[nodiscard]] double arrowInset(ArrowStyle style, double lineWidth);

// Page-space box covering the items (shapes with their outside text,
// connectors with their labels); every item when `ids` is empty.
[[nodiscard]] QRectF itemsBounds(const Page &page, const QSet<QString> &ids = {});
// The label box of a shape in page space (for the in-place text editor):
// its text rectangle, unrotated, positioned at the rotated centre.
[[nodiscard]] QRectF textBox(const Shape &shape);

// A thumbnail of a stencil in the drawing's theme, for the Shapes panel.
[[nodiscard]] QImage stencilThumbnail(const QString &stencilId, const QString &themeId, int pixels);

} // namespace QindaOffice::Diagram
