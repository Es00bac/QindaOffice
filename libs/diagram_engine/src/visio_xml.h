// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to diagram_engine's Visio interchange (M65, D-073): the VSDX
// vocabulary ([MS-VSDX], Visio 2013 and later) the reader and the writer
// share — relationship and content types, and small helpers for the
// ShapeSheet XML (<Cell N V F U/>, <Section>, <Row>).
//
// Units: every V of a length cell is in INCHES whatever its U says (U is
// only how Visio displays it); angles are radians, counter-clockwise; Y
// grows UP from the bottom of the page (or of the parent group).

#include "ooxml_namespaces.h"
#include "ooxml_xml.h"

#include <QColor>
#include <QString>
#include <QStringView>

namespace QindaOffice::Diagram::Visio {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

inline constexpr const char16_t *kNs = Ooxml::Ns::VISIO;

namespace Rel {
inline constexpr char16_t Document[] = u"http://schemas.microsoft.com/visio/2010/relationships/document";
inline constexpr char16_t Pages[] = u"http://schemas.microsoft.com/visio/2010/relationships/pages";
inline constexpr char16_t Page[] = u"http://schemas.microsoft.com/visio/2010/relationships/page";
inline constexpr char16_t Masters[] = u"http://schemas.microsoft.com/visio/2010/relationships/masters";
inline constexpr char16_t Master[] = u"http://schemas.microsoft.com/visio/2010/relationships/master";
inline constexpr char16_t Windows[] = u"http://schemas.microsoft.com/visio/2010/relationships/windows";
inline constexpr char16_t CustomProperties[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties";
} // namespace Rel

namespace Type {
inline constexpr char16_t Drawing[] = u"application/vnd.ms-visio.drawing.main+xml";
inline constexpr char16_t DrawingMacro[] = u"application/vnd.ms-visio.drawing.macroEnabled.main+xml";
inline constexpr char16_t Template[] = u"application/vnd.ms-visio.template.main+xml";
inline constexpr char16_t TemplateMacro[] = u"application/vnd.ms-visio.template.macroEnabled.main+xml";
inline constexpr char16_t Pages[] = u"application/vnd.ms-visio.pages+xml";
inline constexpr char16_t Page[] = u"application/vnd.ms-visio.page+xml";
inline constexpr char16_t Masters[] = u"application/vnd.ms-visio.masters+xml";
inline constexpr char16_t Master[] = u"application/vnd.ms-visio.master+xml";
inline constexpr char16_t Windows[] = u"application/vnd.ms-visio.windows+xml";
inline constexpr char16_t CustomProperties[] =
    u"application/vnd.openxmlformats-officedocument.custom-properties+xml";
} // namespace Type

// Page units (1/96 in) per inch.
inline constexpr double kPx = 96.0;

// First / all child elements of `e` in the Visio namespace named `name`.
[[nodiscard]] const XmlElement *child(const XmlElement &e, QStringView name);
[[nodiscard]] XmlElement *child(XmlElement &e, QStringView name);
[[nodiscard]] QList<const XmlElement *> children(const XmlElement &e, QStringView name);
[[nodiscard]] XmlElementPtr makeElement(const QString &name);
XmlElement &appendElement(XmlElement &parent, const QString &name);
// An unqualified attribute as an int (Visio IDs, IX), or `fallback`.
[[nodiscard]] int intAttribute(const XmlElement &e, QStringView name, int fallback = -1);

// Visio's spelling of a number: up to 16 significant digits, no exponent
// for ordinary magnitudes, "0" for anything not finite.
[[nodiscard]] QString numberText(double value);

// <Cell N=… V=… [U=…] [F=…]/>: appended, or replacing the cell named `name`
// among `parent`'s direct children (keeping its position).
XmlElement &appendCell(XmlElement &parent, const QString &name, const QString &value, const QString &formula = {},
                       const QString &unit = {});
XmlElement &setCell(XmlElement &parent, const QString &name, const QString &value, const QString &formula = {},
                    const QString &unit = {});
void removeCell(XmlElement &parent, QStringView name);
// The direct child <Section N=name [IX=ix]>, or nullptr.
[[nodiscard]] XmlElement *section(XmlElement &shape, QStringView name, int ix = -1);
void removeSections(XmlElement &shape, QStringView name);

// "#rrggbb" in lower case, as Visio writes colours.
[[nodiscard]] QString colorText(const QColor &color);

} // namespace QindaOffice::Diagram::Visio
