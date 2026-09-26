// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Diagram themes (M50, D-050): palettes that are DOCUMENT data, not chrome.
// A theme names a fill/line pair per StyleRole, the connector and text
// colours and the document font; new shapes take their colours from the
// drawing's theme, and Design ▸ Themes restyles existing ones. The paper
// rule (D-004) still holds for the page itself: white unless the author
// picks a background.

#include "drawing.h"
#include "stencils.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace QindaOffice::Diagram {

struct ColorPair {
    QColor fill;
    QColor line;
};

struct Theme {
    QString id;
    QString name;              // translated
    QVector<ColorPair> roles;  // indexed by StyleRole (Plain excluded)
    QColor connector;
    QColor text;
    QString font;
};

[[nodiscard]] const QVector<Theme> &themes();
// The theme `id`, or the first theme.
[[nodiscard]] const Theme &theme(const QString &id);
[[nodiscard]] ColorPair roleColors(const Theme &theme, StyleRole role);

// The style, text style and connector look a new item takes.
[[nodiscard]] ShapeStyle shapeStyleFor(const Theme &theme, const Stencil *stencil);
[[nodiscard]] TextStyle textStyleFor(const Theme &theme, const Stencil *stencil);
void styleConnector(const Theme &theme, Connector &connector);

// "Quick styles": variant 0..5 recolours a shape with the theme's role
// pair of that index (a one-click restyle that stays inside the palette).
[[nodiscard]] ShapeStyle quickStyle(const Theme &theme, int variant, const ShapeStyle &base);

// Restyles every shape and connector of `drawing` to theme `id` (fills,
// lines, text colour and font; sizes, patterns and shadows stay) and makes
// it the drawing's theme. False for an unknown id.
bool applyTheme(Drawing &drawing, const QString &id);

} // namespace QindaOffice::Diagram
