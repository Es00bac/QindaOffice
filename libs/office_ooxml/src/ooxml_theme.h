// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DrawingML themes and colours (ECMA-376 Part 1 §20.1.6, §20.1.2.3).
//
// A theme part names twelve scheme colours (dk1, lt1, dk2, lt2,
// accent1–6, hlink, folHlink) and a major/minor font pair. Documents refer
// to them indirectly: WordprocessingML through w:themeColor and
// w:asciiTheme, DrawingML through a:schemeClr (via the slide master's
// colour map: bg1 → lt1, tx1 → dk1, …). resolveColor() evaluates any
// DrawingML colour element, including its transforms (lumMod, lumOff,
// tint, shade, alpha, satMod, …), the way PowerPoint renders them.

#include "ooxml_xml.h"

#include <QColor>
#include <QHash>
#include <QString>

namespace QindaOffice::Ooxml {

struct Theme {
    QString name;
    QHash<QString, QColor> colors; // "dk1", "accent1", … (lower-case keys as written)
    QString majorLatin;            // heading font, e.g. "Calibri Light" / "Aptos Display"
    QString minorLatin;            // body font, e.g. "Calibri" / "Aptos"
    QString majorEastAsian;
    QString minorEastAsian;

    [[nodiscard]] bool isValid() const { return !colors.isEmpty(); }
};

// Parses an a:theme root. Missing pieces stay empty.
[[nodiscard]] Theme parseTheme(const XmlElement &themeRoot);

// The Office 2013–2022 default theme ("Office Theme": Calibri/Calibri
// Light, the familiar blue accent1 4472C4), used when a document has no
// theme part.
[[nodiscard]] const Theme &defaultOfficeTheme();

// Maps scheme names used inside shapes (bg1, tx1, bg2, tx2, phClr) to theme
// slots; a slide master's p:clrMap supplies it. Empty map = Office default
// (bg1→lt1, tx1→dk1, bg2→lt2, tx2→dk2).
using ColorMap = QHash<QString, QString>;

// Resolves a DrawingML colour element (a:srgbClr, a:schemeClr, a:sysClr,
// a:prstClr, a:scrgbClr, a:hslClr) with its child transforms. `phColor`
// substitutes for a:schemeClr val="phClr" (theme style matrices).
// Invalid QColor when `color` is not a colour element.
[[nodiscard]] QColor resolveColor(const XmlElement *color, const Theme &theme, const ColorMap &map = {},
                                  const QColor &phColor = QColor());

// Finds the colour element directly inside `parent` (a:solidFill,
// a:fgClr, w14:… containers) and resolves it.
[[nodiscard]] QColor resolveChildColor(const XmlElement *parent, const Theme &theme, const ColorMap &map = {},
                                       const QColor &phColor = QColor());

// Applies DrawingML colour transforms in document order (exposed for the
// WordprocessingML w:themeTint / w:themeShade attributes, which use the
// same arithmetic as a:tint / a:shade).
[[nodiscard]] QColor applyTint(const QColor &color, double tint);   // 0..1, 1 = unchanged
[[nodiscard]] QColor applyShade(const QColor &color, double shade); // 0..1, 1 = unchanged

// Word's named highlight colours (w:highlight w:val) ↔ RGB.
[[nodiscard]] QColor highlightColor(const QString &name);
[[nodiscard]] QString nearestHighlightName(const QColor &color);

} // namespace QindaOffice::Ooxml
