// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio reader (M65, D-073): document-wide context — the
// style sheets, the colour palette, font face names and the theme — and
// `Lookup`, a shape's resolved sheet read through that context: a cell the
// shape and its master lack comes from the style chain, colours given as a
// palette index or "Themed" become real colours, fonts become names.
//
// AGENT-NOTE (themes, approximated): Visio 2013+ stores most look of a
// themed shape as V="Themed". Colours resolve through the QuickStyle*Color
// cells: 0 dk1, 1 lt1, 2–7 accent1–6, 8 the background, 100–106 the theme's
// variation colours (vt:variationClrScheme, the page's VariationColorIndex
// picking the scheme) — the mapping libvisio also uses. Visio then varies
// them by the QuickStyle matrices (lighter fills, darker lines); those
// matrices are not modelled: themed lines are drawn a quarter darker than
// their colour, themed text is light on a dark fill and dark elsewhere.

#include "visio_sheet.h"

#include "ooxml_theme.h"

#include <QColor>
#include <QHash>
#include <QList>

namespace QindaOffice::Diagram::Visio {

enum class ColorRole { Line, Fill, Font, Shadow, Other };

struct DocumentInfo {
    Styles styles;
    QHash<int, QColor> palette; // the 24 standard Visio colours + <Colors>
    QHash<int, QString> faces;  // <FaceNames> by ID (or position)
    Ooxml::Theme theme;         // the Office default when the file has none
    QList<QList<QColor>> variations; // varColor1..7 of each variation scheme
    double themeLineWeight = 0.75 / 72.0; // inches: the theme's first line style
    int defaultLineStyle = -1;
    int defaultFillStyle = -1;
    int defaultTextStyle = -1;

    // `document` is <VisioDocument>; `themeRoot` its a:theme or nullptr.
    void load(const XmlElement &document, const XmlElement *themeRoot);
    [[nodiscard]] QColor paletteColor(int index) const;
    // A QuickStyle colour index (see the AGENT-NOTE) in variation `variation`.
    [[nodiscard]] QColor themeColor(int index, int variation) const;
    [[nodiscard]] QString themeFont() const;
};

class Lookup {
public:
    Lookup(const Sheet &sheet, const DocumentInfo &doc, int variation = 0);

    [[nodiscard]] const Sheet &sheet() const { return m_sheet; }
    // The shape's (or master's) cell, else the style chain's.
    [[nodiscard]] const Cell *cell(QStringView name) const;
    [[nodiscard]] std::optional<double> number(QStringView name) const;
    [[nodiscard]] double number(QStringView name, double fallback) const;
    // A colour cell resolved (palette, #rrggbb, Themed); invalid when the
    // cell is missing or a themed font colour (the caller knows the fill).
    [[nodiscard]] QColor color(QStringView name, ColorRole role) const;
    [[nodiscard]] QColor colorOf(const Cell *cell, ColorRole role) const;
    // Character / Paragraph row `ix`: the shape's row, else the text style
    // chain's row `ix`, else its row 0.
    [[nodiscard]] const Cell *textCell(QStringView section, int ix, QStringView name) const;
    [[nodiscard]] QString fontName(const Cell *cell) const;
    // A themed number (line weight) or `fallback` when not themed.
    [[nodiscard]] double lineWeight() const;

private:
    [[nodiscard]] int styleFor(Category category) const;

    const Sheet &m_sheet;
    const DocumentInfo &m_doc;
    int m_variation;
};

} // namespace QindaOffice::Diagram::Visio
