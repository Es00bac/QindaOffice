// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OMML in and out (M92, D-100): an m:oMathPara or m:oMath element read into
// the equation model, and the model written back.
//
// AGENT-CONTRACT (office_math ⇄ office_docx, show_model): the apps keep an
// equation as the self-contained fragment Ooxml::serializeFragment makes of
// the source element (office_docx stores it in Docx::kInlineXml). Reading
// that fragment and writing the model back without an edit yields an
// EQUIVALENT element — same elements, attributes, text and order; only
// namespace prefixes and indentation may differ — so an unedited equation
// round-trips through either app (D-068). The writer emits nothing it did
// not read or the model did not change.
//
// What the model cannot interpret is kept, never dropped: property
// elements stay XML (math_model.h), a child that is not math where math
// may stand (w:bookmarkStart, w:ins, w:proofErr, a later Office's element)
// becomes a Kept node, and a structure missing a required argument or
// holding them out of order is Kept whole.

#include "math_model.h"

#include "ooxml_xml.h"

#include <QString>

#include <optional>

namespace QindaOffice::Math {

// m:oMathPara (display) or m:oMath (inline); nullopt for anything else.
[[nodiscard]] std::optional<Equation> readOmml(const Ooxml::XmlElement &element);
// The stored fragment form (serializeFragment of either element).
[[nodiscard]] std::optional<Equation> readOmmlFragment(const QString &fragment);

// m:oMathPara for a display equation (or one with several lines), m:oMath
// for an inline one.
[[nodiscard]] Ooxml::XmlElementPtr writeOmml(const Equation &equation);
[[nodiscard]] QString writeOmmlFragment(const Equation &equation);

// The math a Kept node shows: the content of a revision, content control,
// smart tag, custom XML or hyperlink wrapper (an empty argument for a
// deletion), the text of an unknown math element as a run; nullopt for
// anything that shows nothing (bookmarks, proofing marks, permissions).
[[nodiscard]] std::optional<Argument> keptMath(const Node &kept);

} // namespace QindaOffice::Math
