// SPDX-License-Identifier: GPL-3.0-or-later
// Hyperlinks, outline levels and the source journal under structural edits
// (structure_internal.h, M66, D-075).

#include "structure_internal.h"

#include "sheet.h"
#include "workbook.h"

#include <algorithm>

namespace QindaOffice::Calc::StructureMath {

namespace {

// A link's range on the edited sheet, or nullopt when its cells are gone.
std::optional<CellRange> movedLinkRange(CellRange range, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    qint32 &lo = rows ? range.topLeft.row : range.topLeft.col;
    qint32 &hi = rows ? range.bottomRight.row : range.bottomRight.col;
    if (!movedSpan(lo, hi, band)) {
        return std::nullopt;
    }
    return range;
}

// The outline of one axis after the edit. Lines inserted inside a group
// join it — they take the shallower of their two neighbours' levels, so an
// insert at a group's edge stays outside it — the way Excel grows a group.
QMap<qint32, LineOutline> movedLines(const QMap<qint32, LineOutline> &lines, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    QMap<qint32, LineOutline> out;
    for (auto it = lines.constBegin(); it != lines.constEnd(); ++it) {
        const qint32 moved = movedIndex(it.key(), band);
        if (moved >= 0) {
            out.insert(moved, it.value());
        }
    }
    if (band.insert && band.at > 0) {
        const quint8 level = std::min(out.value(band.at - 1).level, out.value(band.at + band.count).level);
        for (qint32 i = band.at; level > 0 && i < band.at + band.count; ++i) {
            out.insert(i, LineOutline{level, false, false});
        }
    }
    return out;
}

} // namespace

void moveSheetFeatures(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    Sheet &edited = book.sheetAt(edit.sheet);
    QList<Hyperlink> links;
    for (Hyperlink link : edited.hyperlinks()) {
        if (const auto moved = movedLinkRange(link.range, edit)) {
            link.range = *moved;
            links.append(link);
        }
    }
    edited.setHyperlinks(std::move(links));
    // Internal locations follow the cells they name, like a formula's
    // reference does; one whose cells were removed keeps its text (Excel
    // keeps a dead link rather than dropping it).
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<Hyperlink> sheetLinks = book.sheetAt(s).hyperlinks();
        bool touched = false;
        for (Hyperlink &link : sheetLinks) {
            const ParseResult parsed = parseFormula(link.location);
            if (!link.isInternal() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
                continue;
            }
            RangeRef ref = parsed.root->ref;
            if (adjustRef(ref, edit, s == edit.sheet, editedSheetName) && !(ref == parsed.root->ref)) {
                link.location = rangeRefText(ref);
                touched = true;
            }
        }
        if (touched) {
            book.sheetAt(s).setHyperlinks(std::move(sheetLinks));
        }
    }
    SheetOutline outline = edited.outline();
    QMap<qint32, LineOutline> &lines = editsRows(edit) ? outline.rows : outline.cols;
    lines = movedLines(lines, edit);
    edited.setOutline(std::move(outline));
    edited.recordStructureStep({editsRows(edit), bandOf(edit).insert, edit.at, edit.count});
}

void requalifyLinkLocations(Workbook &book, const QString &from, const QString &to)
{
    if (to.isEmpty()) {
        return;
    }
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<Hyperlink> links = book.sheetAt(s).hyperlinks();
        bool touched = false;
        for (Hyperlink &link : links) {
            const ParseResult parsed = parseFormula(link.location);
            if (!link.isInternal() || !parsed.root || parsed.root->kind != Node::Kind::Ref
                || unquotedSheetName(parsed.root->ref.sheet).compare(from, Qt::CaseInsensitive) != 0) {
                continue;
            }
            RangeRef ref = parsed.root->ref;
            ref.sheet = to;
            link.location = rangeRefText(ref);
            touched = true;
        }
        if (touched) {
            book.sheetAt(s).setHyperlinks(std::move(links));
        }
    }
}

} // namespace QindaOffice::Calc::StructureMath
