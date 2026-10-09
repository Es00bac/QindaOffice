// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Cell hyperlinks (M66, D-075): Excel's <hyperlink> model. A link covers a
// range (usually one cell) and leads either OUT of the workbook — a URL, a
// mailto: address, a file — or to a place IN it ('Q1 Sales'!B2, a defined
// name). The link is not the cell's content: the cell keeps its own value
// (the text Excel shows), the link rides alongside it like a comment, so
// structure.cpp moves it with the rows and columns it covers and a sheet
// rename re-points internal locations at the new name.
//
// The HYPERLINK() function is different: its link is the formula's first
// argument, evaluated on demand (hyperlinkFormulaTarget), so it stays live
// with the data it is built from.

#include "value.h"

#include <QString>

#include <optional>

namespace QindaOffice::Calc {

class Workbook;

struct Hyperlink {
    CellRange range;  // the cells it covers (Excel's ref)
    QString address;  // external target, #fragment included; empty for an internal link
    QString location; // in-workbook target ("'Q1 Sales'!B2", a defined name) when address is empty
    QString display;  // Excel's display attribute: the text the link was made with (kept, informational)
    QString tooltip;  // the screen tip
    bool operator==(const Hyperlink &) const = default;

    [[nodiscard]] bool isInternal() const { return address.isEmpty(); }
};

// What a link opens, the way Excel's dialog spells it: the address, or
// "#location" for a place in this workbook.
[[nodiscard]] QString hyperlinkTarget(const Hyperlink &link);

// The other direction, for a typed address: "#Sheet2!A1" is a location in
// this workbook; anything else is an address, "www.example.com" gaining
// http:// and "a@b.com" gaining mailto: the way Excel completes them.
void setHyperlinkTarget(Hyperlink *link, const QString &typed);

// Where an internal location points: a sheet index and range, resolved
// from `fromSheet` (an unqualified "B3" means the link's own sheet). A
// defined name standing for a range resolves too. nullopt when it names
// nothing this workbook has.
struct LinkPlace {
    qint32 sheet = -1;
    CellRange range;
};
[[nodiscard]] std::optional<LinkPlace> resolveLinkLocation(const Workbook &book, qint32 fromSheet,
                                                          const QString &location);

// The link a HYPERLINK() formula in the cell makes (its first argument,
// evaluated in the cell), or empty when the cell's formula is not a
// HYPERLINK call at the top.
[[nodiscard]] QString hyperlinkFormulaTarget(const Workbook &book, qint32 sheet, const CellAddr &addr);

} // namespace QindaOffice::Calc
