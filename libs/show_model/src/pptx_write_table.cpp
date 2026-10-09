// SPDX-License-Identifier: GPL-3.0-or-later
// a:tbl for the pptx writer (pptx_write_shapes.h; M62, reworked by M67,
// D-076). A table read from the deck is written back THROUGH its source:
// every row, column and cell maps to the one it was read as by TableOrigin
// (show_table.h), so
//   - an unedited cell is its source a:tc, byte for byte, wherever rows or
//     columns were inserted or deleted around it;
//   - a source row / column keeps its a:tr / a:gridCol, extensions (the
//     a16:rowId / a16:colId PowerPoint writes) included;
//   - a:tblPr is the source's, only the changed style options and style id
//     patched into it;
//   - an edited cell reuses the source's a:pPr / a:rPr for text whose
//     formatting it kept — compared with the table style's look of its
//     region factored out, so text that only moved into another region
//     (a row inserted above the header) keeps its source spelling.
// Text the writer spells itself leaves colour and weight to the table
// style wherever they ARE the style's, so the table keeps restyling itself
// in PowerPoint (table_edit.h).

#include "pptx_write_shapes.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const QString &name, const QString &value)
{
    e.setAttribute(QString(), name, value);
}

XmlElementPtr carried(PartWriter &out, const XmlElement &source, const QString &sourcePart)
{
    XmlElementPtr copy = source.clone();
    out.carry(*copy, sourcePart);
    return copy;
}

XmlElementPtr borderElement(const char *name, const CellBorder &border)
{
    LineStyle line;
    line.dash = border.dash;
    XmlElementPtr ln = lineElement(border.color, border.width, line, border.visible());
    ln->name = QLatin1String(name);
    return ln;
}

XmlElementPtr cellProperties(PartWriter &out, const TableCell &cell)
{
    XmlElementPtr tcPr = make(nsA(), QStringLiteral("tcPr"));
    const TableCell defaults;
    const auto margin = [&](const char *name, qreal value, qreal fallback) {
        if (value != fallback) {
            set(*tcPr, QLatin1String(name), emu(value));
        }
    };
    margin("marL", cell.marginLeft, defaults.marginLeft);
    margin("marR", cell.marginRight, defaults.marginRight);
    margin("marT", cell.marginTop, defaults.marginTop);
    margin("marB", cell.marginBottom, defaults.marginBottom);
    static const char *const kAnchors[] = {"t", "ctr", "b", "just", "dist"};
    if (cell.anchor != TextAnchor::Top) {
        set(*tcPr, QStringLiteral("anchor"), QLatin1String(kAnchors[int(cell.anchor)]));
    }
    for (const auto &[border, name] : {std::pair{&cell.left, "lnL"}, std::pair{&cell.right, "lnR"},
                                       std::pair{&cell.top, "lnT"}, std::pair{&cell.bottom, "lnB"}}) {
        if (border->set) {
            tcPr->appendElement(borderElement(name, *border));
        }
    }
    if (cell.fillStyle.kind != FillKind::Plain) {
        tcPr->appendElement(fillElement(out, QColor(), cell.fillStyle));
    } else if (cell.noFill) {
        tcPr->appendElement(nsA(), QStringLiteral("noFill"));
    } else if (cell.fill.isValid()) {
        tcPr->appendElement(fillElement(out, cell.fill, FillStyle()));
    }
    return tcPr;
}

bool sameCellLook(const TableCell &a, const TableCell &b)
{
    TableCell x = a;
    TableCell y = b;
    x.paragraphs.clear();
    y.paragraphs.clear();
    return x == y;
}

bool sameOptions(const Table &a, const Table &b)
{
    return a.styleId == b.styleId && a.firstRow == b.firstRow && a.firstColumn == b.firstColumn && a.lastRow == b.lastRow
        && a.lastColumn == b.lastColumn && a.bandRows == b.bandRows && a.bandColumns == b.bandColumns;
}

// a:tblPr: the source's (patched where the options changed), or new.
XmlElementPtr tableProperties(PartWriter &out, const Table &table, const XmlElement *sourcePr, const Table *pristine,
                              const QString &sourcePart)
{
    if (sourcePr != nullptr && pristine != nullptr && sameOptions(table, *pristine)) {
        return carried(out, *sourcePr, sourcePart);
    }
    XmlElementPtr pr = sourcePr != nullptr ? carried(out, *sourcePr, sourcePart) : make(nsA(), QStringLiteral("tblPr"));
    for (const auto &[on, name] : {std::pair{table.firstRow, u"firstRow"}, std::pair{table.firstColumn, u"firstCol"},
                                   std::pair{table.lastRow, u"lastRow"}, std::pair{table.lastColumn, u"lastCol"},
                                   std::pair{table.bandRows, u"bandRow"}, std::pair{table.bandColumns, u"bandCol"}}) {
        if (on) {
            set(*pr, QString::fromUtf16(name), QStringLiteral("1"));
        } else {
            pr->removeAttribute(QString(), name);
        }
    }
    if (pristine == nullptr || table.styleId != pristine->styleId || sourcePr == nullptr) {
        pr->removeChildren(nsA(), u"tableStyleId");
        if (!table.styleId.isEmpty()) {
            pr->removeChildren(nsA(), u"tableStyle"); // an inline style is the other choice
            XmlElementPtr id = make(nsA(), QStringLiteral("tableStyleId"));
            id->appendText(table.styleId);
            pr->insertElementBefore(id, {u"extLst"});
        }
    }
    return pr;
}

// The table style's look of a region factored out of a run: the colour it
// gives becomes "the region's" (invalid), the weight a deviation flag.
TextRun regionRelative(TextRun run, const TableRegion &region, const QColor &plain)
{
    const QColor text = region.text.isValid() ? region.text : plain;
    if (run.color == text) {
        run.color = QColor();
    }
    run.bold = run.bold != region.bold;
    return run;
}

QColor plainText(const Table &table)
{
    return table.look.whole.text.isValid() ? table.look.whole.text : QColor(Qt::black);
}

XmlElementPtr cellElement(PartWriter &out, const Table &table, int r, int c, const XmlElement *sourceCell,
                          const TableCell *was, const TableRegion &wasRegion, const Table *pristine, const QString &sourcePart)
{
    const TableCell &cell = table.rows.at(r).cells.at(c);
    XmlElementPtr tc = make(nsA(), QStringLiteral("tc"));
    if (cell.gridSpan > 1) {
        set(*tc, QStringLiteral("gridSpan"), QString::number(cell.gridSpan));
    }
    if (cell.rowSpan > 1) {
        set(*tc, QStringLiteral("rowSpan"), QString::number(cell.rowSpan));
    }
    if (cell.hMerge) {
        set(*tc, QStringLiteral("hMerge"), QStringLiteral("1"));
    }
    if (cell.vMerge) {
        set(*tc, QStringLiteral("vMerge"), QStringLiteral("1"));
    }
    const TableRegion region = table.regionAt(r, c);
    const QColor plain = plainText(table);
    const QColor wasPlain = pristine != nullptr ? plainText(*pristine) : plain;
    const TextBodyStyle body;
    TextSource text{sourceCell != nullptr ? sourceCell->child(nsA(), u"txBody") : nullptr,
                    was != nullptr ? &was->paragraphs : nullptr, &body, sourcePart};
    text.sourceKey = [wasRegion, wasPlain](const TextRun &run) { return regionRelative(run, wasRegion, wasPlain); };
    text.currentKey = [region, plain](const TextRun &run) { return regionRelative(run, region, plain); };
    text.generatedRun = [region, plain](XmlElement &rPr, const TextRun &run) {
        // What the table style supplies is left to it.
        if (run.color == (region.text.isValid() ? region.text : plain)) {
            rPr.removeChildren(nsA(), u"solidFill");
        }
        if (run.bold == region.bold) {
            rPr.removeAttribute(QString(), u"b");
        }
    };
    XmlElementPtr txBody = textBodyElement(out, nsA(), cell.paragraphs, body, text);
    if (text.txBody == nullptr) {
        // Cell margins live in a:tcPr; the cell's own bodyPr stays bare.
        txBody->removeChildren(nsA(), u"bodyPr");
        txBody->insertElementBefore(make(nsA(), QStringLiteral("bodyPr")), {u"lstStyle", u"p"});
    }
    tc->appendElement(txBody);
    const XmlElement *sourceTcPr = sourceCell != nullptr ? sourceCell->child(nsA(), u"tcPr") : nullptr;
    if (sourceTcPr != nullptr && was != nullptr && sameCellLook(cell, *was)) {
        tc->appendElement(carried(out, *sourceTcPr, sourcePart));
    } else {
        tc->appendElement(cellProperties(out, cell));
    }
    if (const XmlElement *ext = sourceCell != nullptr ? sourceCell->child(nsA(), u"extLst") : nullptr) {
        tc->appendElement(carried(out, *ext, sourcePart));
    }
    return tc;
}

} // namespace

XmlElementPtr tableElement(PartWriter &out, const Table &table, const XmlElement *source, const Table *pristine,
                           const QString &sourcePart)
{
    XmlElementPtr tbl = make(nsA(), QStringLiteral("tbl"));
    const XmlElement *sourcePr = source != nullptr ? source->child(nsA(), u"tblPr") : nullptr;
    tbl->appendElement(tableProperties(out, table, sourcePr, pristine, sourcePart));
    // AGENT-GUARD: rows, columns and cells map to the source only through
    // TableOrigin, and only while it matches the table; a table whose
    // origin does not (pasted, made in QindaShow) is written fresh.
    const TableOrigin &origin = table.origin;
    const bool mapped = source != nullptr && pristine != nullptr && origin.rows.size() == table.rows.size()
        && origin.columns.size() == table.columns.size();
    const XmlElement *sourceGrid = source != nullptr ? source->child(nsA(), u"tblGrid") : nullptr;
    const QList<const XmlElement *> sourceColumns = sourceGrid != nullptr ? sourceGrid->elements(nsA(), u"gridCol")
                                                                          : QList<const XmlElement *>();
    XmlElement &grid = tbl->appendElement(nsA(), QStringLiteral("tblGrid"));
    for (int c = 0; c < table.columns.size(); ++c) {
        const int sc = mapped ? origin.sourceColumn(c) : -1;
        if (sc >= 0 && sc < sourceColumns.size()) {
            XmlElementPtr column = carried(out, *sourceColumns.at(sc), sourcePart);
            if (sc >= pristine->columns.size() || pristine->columns.at(sc) != table.columns.at(c)) {
                set(*column, QStringLiteral("w"), emu(table.columns.at(c)));
            }
            grid.appendElement(column);
        } else {
            set(grid.appendElement(nsA(), QStringLiteral("gridCol")), QStringLiteral("w"), emu(table.columns.at(c)));
        }
    }
    const QList<const XmlElement *> sourceRows = source != nullptr ? source->elements(nsA(), u"tr") : QList<const XmlElement *>();
    for (int r = 0; r < table.rows.size(); ++r) {
        const TableRow &row = table.rows.at(r);
        const int sr = mapped ? origin.sourceRow(r) : -1;
        const XmlElement *sourceRow = sr >= 0 && sr < sourceRows.size() ? sourceRows.at(sr) : nullptr;
        const TableRow *wasRow = sourceRow != nullptr && sr < pristine->rows.size() ? &pristine->rows.at(sr) : nullptr;
        XmlElementPtr tr = make(nsA(), QStringLiteral("tr"));
        if (sourceRow != nullptr) {
            tr->attributes = sourceRow->attributes;
        }
        if (wasRow == nullptr || wasRow->height != row.height || !tr->hasAttribute(QString(), u"h")) {
            set(*tr, QStringLiteral("h"), emu(row.height));
        }
        const QList<const XmlElement *> sourceCells = sourceRow != nullptr ? sourceRow->elements(nsA(), u"tc")
                                                                           : QList<const XmlElement *>();
        for (int c = 0; c < row.cells.size(); ++c) {
            const int sc = mapped ? origin.sourceColumn(c) : -1;
            const XmlElement *sourceCell = sc >= 0 && sc < sourceCells.size() ? sourceCells.at(sc) : nullptr;
            const TableCell *was = wasRow != nullptr && sc >= 0 && sc < wasRow->cells.size() ? &wasRow->cells.at(sc) : nullptr;
            if (sourceCell != nullptr && was != nullptr && *was == row.cells.at(c)) {
                tr->appendElement(carried(out, *sourceCell, sourcePart)); // unedited: byte for byte
                continue;
            }
            const TableRegion wasRegion = was != nullptr ? pristine->regionAt(sr, sc) : TableRegion();
            tr->appendElement(cellElement(out, table, r, c, sourceCell, was, wasRegion, pristine, sourcePart));
        }
        if (const XmlElement *ext = sourceRow != nullptr ? sourceRow->child(nsA(), u"extLst") : nullptr) {
            tr->appendElement(carried(out, *ext, sourcePart));
        }
        tbl->appendElement(tr);
    }
    return tbl;
}

} // namespace QindaOffice::Show::Pptx
