// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pptx reader's resolution machinery (internal to show_model, D-070):
// theme colours through the colour map, fills and outlines (including the
// theme's style matrix behind p:style), and text styles as OPTIONAL levels
// that inheritance merges —
//
//   presentation defaultTextStyle → master title/body/other style →
//   master placeholder lstStyle → layout placeholder lstStyle → p:style
//   fontRef → the shape's lstStyle → a:pPr / a:rPr
//
// — so the reader can store EFFECTIVE values in the model (show_text.h).

#include "presentation.h"

#include "ooxml_package.h"
#include "ooxml_theme.h"
#include "ooxml_xml.h"

#include <array>
#include <optional>

namespace QindaOffice::Show::Pptx {

namespace Ooxml = QindaOffice::Ooxml;

const QString &nsA();
const QString &nsP();
const QString &nsR();
const QString &nsC();
const QString &nsMc();

[[nodiscard]] inline qreal emuToPt(const QString &value, qreal fallback = 0)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    return ok ? v / 12700.0 : fallback;
}

// A theme with its style matrix (a:fmtScheme), for p:style references and
// a:bgRef backgrounds.
struct ThemeContext {
    Ooxml::Theme theme;
    QList<Ooxml::XmlElementPtr> fillStyles;
    QList<Ooxml::XmlElementPtr> lineStyles;
    QList<Ooxml::XmlElementPtr> backgroundStyles;
};
[[nodiscard]] ThemeContext parseThemeContext(const Ooxml::XmlElement *themeRoot);

// Everything an element resolves against: the package and part (for
// relationship ids), the theme and the colour map in force there.
struct PartContext {
    std::shared_ptr<const Ooxml::Package> package;
    QString part;
    const ThemeContext *theme = nullptr;
    Ooxml::ColorMap map;
    const QHash<QString, QString> *slideIds = nullptr; // slide part → model id (hyperlinks)
};

[[nodiscard]] QColor color(const PartContext &ctx, const Ooxml::XmlElement *colorElement, const QColor &phColor = {});
// A colour as the model keeps it: 8 bits a channel.
[[nodiscard]] QColor eightBit(const QColor &color);
// The colour inside a container (a:solidFill, a:fgClr, a:highlight, …).
[[nodiscard]] QColor childColor(const PartContext &ctx, const Ooxml::XmlElement *container, const QColor &phColor = {});

// A fill element (a:noFill, a:solidFill, a:gradFill, a:blipFill,
// a:pattFill) or nothing. `plain` gets the solid colour (or the first
// gradient stop as a representative).
struct ResolvedFill {
    bool present = false; // a fill element was found
    QColor plain = Qt::transparent;
    FillStyle style;
};
[[nodiscard]] ResolvedFill fillFrom(const PartContext &ctx, const Ooxml::XmlElement *parent,
                                    const QColor &phColor = {});
// a:fillRef / a:bgRef: an entry of the theme's style matrix.
[[nodiscard]] ResolvedFill fillFromReference(const PartContext &ctx, const Ooxml::XmlElement *ref);

struct ResolvedLine {
    bool present = false;
    bool colorSet = false; // the a:ln spelled its own fill (else the style's applies)
    QColor color = Qt::transparent;
    qreal width = 0;
    LineStyle style;
};
[[nodiscard]] ResolvedLine lineFrom(const PartContext &ctx, const Ooxml::XmlElement *ln, const QColor &phColor = {});
[[nodiscard]] ResolvedLine lineFromReference(const PartContext &ctx, const Ooxml::XmlElement *lnRef);

// ---- text styles --------------------------------------------------------------

struct RunProps {
    std::optional<qreal> size;
    std::optional<bool> bold, italic;
    std::optional<QString> underline, strike, caps, latin;
    std::optional<int> baseline;
    std::optional<qreal> spacing;
    std::optional<QColor> color, highlight;
    void overlay(const RunProps &more);
};

struct ParaProps {
    std::optional<int> alignment;
    std::optional<qreal> marginLeft, indent;
    std::optional<Spacing> lineSpacing, spaceBefore, spaceAfter;
    std::optional<int> bulletKind; // 0 none, 1 character, 2 auto-number
    std::optional<QString> bulletChar, autoNumber, bulletFont;
    std::optional<int> autoNumberStart;
    std::optional<QColor> bulletColor; // invalid = follow the text
    std::optional<qreal> bulletSize;   // percent; <0 = absolute points (negated)
    RunProps run;                      // a:defRPr
    void overlay(const ParaProps &more);
};

struct ListStyle {
    std::array<ParaProps, 9> levels;
    void overlay(const ListStyle &more);
};

[[nodiscard]] RunProps parseRunProps(const PartContext &ctx, const Ooxml::XmlElement *rPr);
[[nodiscard]] ParaProps parseParaProps(const PartContext &ctx, const Ooxml::XmlElement *pPr);
// a:lstStyle, p:titleStyle, p:bodyStyle, p:otherStyle, defaultTextStyle.
[[nodiscard]] ListStyle parseListStyle(const PartContext &ctx, const Ooxml::XmlElement *list);

// a:bodyPr elements applied in order (inherited first).
void applyBodyProps(TextBodyStyle &style, const Ooxml::XmlElement *bodyPr);
[[nodiscard]] TextBodyStyle officeBodyDefaults();

// A theme font reference ("+mn-lt") resolved to a family.
[[nodiscard]] QString resolveTypeface(const PartContext &ctx, const QString &typeface);

// The bytes and media type of a related part (r:embed / r:link …).
struct RelatedData {
    QByteArray bytes;
    QString mime;
    QString partName;
};
[[nodiscard]] RelatedData relatedData(const PartContext &ctx, const QString &relationshipId);

// A hyperlink element (a:hlinkClick) as the model spells it.
[[nodiscard]] QString hyperlinkFrom(const PartContext &ctx, const Ooxml::XmlElement *hlink);

// QindaShow's layout name for an Office layout type (ST_SlideLayoutType).
[[nodiscard]] QString appLayoutName(const QString &type);

} // namespace QindaOffice::Show::Pptx
