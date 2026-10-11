// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the carry stage (xlsx_carry*.cpp, M66, D-075): which
// relationships and elements the writer makes itself and which travel
// from the source package. One table for the read side (what to keep a
// copy of) and the save side (what to copy).

#include <QSet>
#include <QString>

namespace QindaOffice::Calc::IO::Carry {

// The last segment of a relationship type ("…/relationships/pivotTable").
inline QString relKind(const QString &type)
{
    return type.mid(type.lastIndexOf(QLatin1Char('/')) + 1);
}

// Workbook relationships the writer makes itself: never carried.
inline bool workbookRelWritten(const QString &type)
{
    static const QSet<QString> kinds{QStringLiteral("worksheet"), QStringLiteral("chartsheet"),
                                     QStringLiteral("dialogsheet"), QStringLiteral("macrosheet"),
                                     QStringLiteral("xlMacrosheet"), QStringLiteral("xlIntlMacrosheet"),
                                     QStringLiteral("styles"), QStringLiteral("sharedStrings"), QStringLiteral("theme"),
                                     QStringLiteral("calcChain"), QStringLiteral("sheetMetadata")};
    return kinds.contains(relKind(type));
}

// Pictures in cells and linked data types: rich values the cells point at
// with vm attributes the engine does not keep. Never carried (warned).
inline bool isRichDataRel(const QString &type)
{
    const QString kind = relKind(type);
    return kind.startsWith(QLatin1String("rd")) || kind == QLatin1String("richStyles")
        || kind == QLatin1String("richValueRel");
}

// Package relationships (_rels/.rels) carried: the ribbon customisation and
// the custom document properties. Core and app properties are rewritten,
// a thumbnail would show the old content, a signature no longer holds.
inline bool packageRelCarried(const QString &type)
{
    const QString kind = relKind(type);
    return kind == QLatin1String("extensibility") || kind == QLatin1String("custom-properties");
}

inline bool isSignatureRel(const QString &type)
{
    return type.contains(QLatin1String("digital-signature"));
}

// Sheet relationships the writer makes itself (the drawing and legacy
// drawing are made anew, carried objects are merged into them).
inline bool sheetRelWritten(const QString &type)
{
    static const QSet<QString> kinds{QStringLiteral("drawing"), QStringLiteral("vmlDrawing"),
                                     QStringLiteral("comments"), QStringLiteral("table"),
                                     QStringLiteral("hyperlink")};
    return kinds.contains(relKind(type));
}

// Sheet relationships no element names: found through the rels alone.
inline bool sheetRelStandalone(const QString &type)
{
    const QString kind = relKind(type);
    return kind == QLatin1String("pivotTable") || kind == QLatin1String("threadedComment");
}

// Sheet elements the writer never writes, carried verbatim (relationship
// ids re-made) when the sheet still exists. `positional` ones name rows or
// columns the journal cannot move (they are carried only when the sheet
// had no structural edit).
struct CarriedElement {
    const char *name;
    bool positional;
};

// M78 (D-087): printOptions, pageMargins, headerFooter and the page breaks
// are modelled (Sheet::PageSetup) and written by the post-pass; pageSetup is
// written from the model with the source's printer-settings link merged in
// (xlsx_carry_sheet.cpp). The pictures a header or footer shows (&G) live
// in a VML part of their own (legacyDrawingHF, carried with its shapes in a
// fresh id block by xlsx_carry_sheet.cpp) or a drawing (drawingHF).
inline constexpr CarriedElement kCarriedSheetElements[] = {
    {"customProperties", false}, {"ignoredErrors", true}, {"drawingHF", false},
    {"picture", false}, {"oleObjects", false}, {"controls", false}, {"webPublishItems", false},
};

// extLst entries of a worksheet that are carried, by uri: slicer lists,
// timeline references, web extensions. M90 (D-098): sparklines are no
// longer carried — the model holds them (xlsx_sparklines.h) — so nothing
// carried here is positional any more.
inline bool sheetExtCarried(const QString &uri, bool *positional)
{
    static const QSet<QString> fixed{QStringLiteral("{A8765BA9-456A-4dab-B4F3-ACF838C121DE}"),
                                     QStringLiteral("{3A4CF648-6AED-40f4-86FF-DC5316D8AED3}"),
                                     QStringLiteral("{7E03D99C-DC04-49d9-9315-930204A7B6E9}"),
                                     QStringLiteral("{F7C9EE02-42E1-4005-9D12-6889AFFD525C}")};
    *positional = false;
    return fixed.contains(uri) || fixed.contains(uri.toUpper());
}

} // namespace QindaOffice::Calc::IO::Carry
