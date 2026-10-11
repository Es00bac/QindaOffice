// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Typed OMML properties, read and written THROUGH a node's property element
// (the AGENT-NOTE in math_model.h): an absent element reads as its ECMA-376
// default; a setter creates the property element when needed and inserts
// its child in schema order, so whatever is written stays valid against
// shared-math.xsd. Property names are the OMML local names (u"chr",
// u"limLoc", …) in the math namespace; values are m:val.

#include "math_model.h"

#include <QString>

#include <optional>

namespace QindaOffice::Math {

enum class FractionType : quint8 { Bar, Skewed, Linear, NoBar };
enum class LimitLocation : quint8 { Default, UnderOver, SubSup };
enum class VerticalPosition : quint8 { Top, Bottom };
enum class DelimiterShape : quint8 { Centered, Match };
// m:sty; Default = absent (letters italic, the rest upright — UTR #25).
enum class MathStyle : quint8 { Default, Plain, Bold, Italic, BoldItalic };
// m:scr
enum class MathScript : quint8 { Roman, Script, Fraktur, DoubleStruck, SansSerif, Monospace };
enum class ColumnAlignment : quint8 { Center, Left, Right };
// m:baseJc of an equation array or matrix (ST_YAlign; inline/inside/outside read as Center).
enum class VerticalJustification : quint8 { Center, Top, Bottom };
// m:oMathParaPr/m:jc and m:defJc.
enum class Justification : quint8 { CenterGroup, Center, Left, Right };

// ---- generic --------------------------------------------------------------------------

// The m:val of property `name`; nullopt when the element is absent, an
// empty string when it is present without a value.
[[nodiscard]] std::optional<QString> propValue(const Node &node, QStringView name);
// OMML on/off: present without a value = on; "0", "false", "off" = off.
[[nodiscard]] bool propOn(const Node &node, QStringView name, bool fallback);
void setPropValue(Node &node, QStringView name, const QString &value);
void setPropOn(Node &node, QStringView name, bool on);
void removeProp(Node &node, QStringView name);

// ---- structures -----------------------------------------------------------------------

[[nodiscard]] FractionType fractionType(const Node &fraction);
void setFractionType(Node &fraction, FractionType type);

// m:nary: the operator (default ∫ U+222B), where its limits go, whether it
// grows with its argument, hidden limits.
[[nodiscard]] QString naryChar(const Node &nary);
[[nodiscard]] LimitLocation limitLocation(const Node &nary);
[[nodiscard]] bool naryGrows(const Node &nary);
[[nodiscard]] bool subHidden(const Node &nary);
[[nodiscard]] bool supHidden(const Node &nary);

// m:d: begin (default "("), separator (default "│" U+2502) and end
// (default ")") characters — "" draws none — growth (default on), shape.
[[nodiscard]] QString delimiterBegin(const Node &d);
[[nodiscard]] QString delimiterSeparator(const Node &d);
[[nodiscard]] QString delimiterEnd(const Node &d);
[[nodiscard]] bool delimiterGrows(const Node &d);
[[nodiscard]] DelimiterShape delimiterShape(const Node &d);

[[nodiscard]] QString accentChar(const Node &acc);               // default U+0302
[[nodiscard]] VerticalPosition barPosition(const Node &bar);     // default bottom
[[nodiscard]] QString groupChar(const Node &groupChr);           // default U+23DF
[[nodiscard]] VerticalPosition groupCharPosition(const Node &groupChr); // default bottom
[[nodiscard]] bool degreeHidden(const Node &radical);
[[nodiscard]] bool alignedScripts(const Node &subSup);           // m:alnScr

struct PhantomFlags {
    bool show = true;
    bool zeroWidth = false;
    bool zeroAscent = false;
    bool zeroDescent = false;
    bool transparent = false;
};
[[nodiscard]] PhantomFlags phantomFlags(const Node &phant);
void setPhantomFlags(Node &phant, const PhantomFlags &flags);

struct BorderFlags {
    bool hideTop = false;
    bool hideBottom = false;
    bool hideLeft = false;
    bool hideRight = false;
    bool strikeHorizontal = false;
    bool strikeVertical = false;
    bool strikeUp = false;   // m:strikeBLTR
    bool strikeDown = false; // m:strikeTLBR
};
[[nodiscard]] BorderFlags borderFlags(const Node &borderBox);

[[nodiscard]] bool boxIsOperator(const Node &box);     // m:opEmu
[[nodiscard]] bool boxIsDifferential(const Node &box); // m:diff

// m:eqArr and m:m.
[[nodiscard]] VerticalJustification baseJustification(const Node &node);
[[nodiscard]] int rowSpacingRule(const Node &node); // m:rSpRule 0–4
[[nodiscard]] int rowSpacing(const Node &node);     // m:rSp
// m:m only.
[[nodiscard]] int columnCount(const Node &matrix);
[[nodiscard]] ColumnAlignment columnAlignment(const Node &matrix, int column); // m:mcs/m:mc
[[nodiscard]] bool placeholdersHidden(const Node &matrix);                    // m:plcHide
[[nodiscard]] int columnGapRule(const Node &matrix);                          // m:cGpRule 0–4
[[nodiscard]] int columnGap(const Node &matrix);                              // m:cGp
[[nodiscard]] int columnSpacing(const Node &matrix);                          // m:cSp (twips)

// ---- runs (m:rPr) ---------------------------------------------------------------------

[[nodiscard]] MathStyle runStyle(const Node &run);
void setRunStyle(Node &run, MathStyle style);
[[nodiscard]] MathScript runScript(const Node &run);
void setRunScript(Node &run, MathScript script);
[[nodiscard]] bool runIsNormalText(const Node &run); // m:nor
void setRunNormalText(Node &run, bool normal);
[[nodiscard]] bool runIsLiteral(const Node &run);    // m:lit
// True when both runs' m:rPr and w:rPr/a:rPr are equal (typed text joins).
[[nodiscard]] bool sameRunFormat(const Node &a, const Node &b);

// ---- arguments and equations ----------------------------------------------------------

[[nodiscard]] int argumentSize(const Argument &argument); // m:argSz, −2…2
[[nodiscard]] Justification justification(const Equation &equation, Justification fallback);
void setJustification(Equation &equation, Justification justification);

// Document-wide math settings: w:settings/m:mathPr (ECMA-376 §22.1.2.62).
struct MathSettings {
    QString mathFont = QStringLiteral("Cambria Math");
    bool smallFraction = false;  // m:smallFrac — fractions stay small in display math
    bool displayDefaults = true; // m:dispDef
    Justification defaultJustification = Justification::CenterGroup;
    LimitLocation integralLimits = LimitLocation::SubSup; // m:intLim
    LimitLocation naryLimits = LimitLocation::UnderOver;  // m:naryLim
    qreal leftMarginPt = 0;  // m:lMargin
    qreal rightMarginPt = 0; // m:rMargin
    qreal spaceBeforePt = 0; // m:preSp
    qreal spaceAfterPt = 0;  // m:postSp

    [[nodiscard]] static MathSettings fromXml(const Ooxml::XmlElement &mathPr);
};

// Equality of two optional property elements (absent == absent).
[[nodiscard]] bool sameXml(const Ooxml::XmlElement *a, const Ooxml::XmlElement *b);

} // namespace QindaOffice::Math
