// SPDX-License-Identifier: GPL-3.0-or-later
// a:tbl for the pptx reader (pptx_read_shapes.h): the grid, rows, cells
// with their text, margins, fills, borders and merges, and the table style
// resolved into a TableLook — from the deck's tableStyles.xml, or for
// PowerPoint's default style (Medium Style 2 – Accent 1) when a producer
// named it without defining it.

#include "pptx_read_shapes.h"

#include "pptx_origin.h"
#include "pptx_read_text.h"
#include "pptx_reader.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

namespace QindaOffice::Show::Pptx {

namespace {

const QString kDefaultStyle = QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}");

CellBorder borderFrom(const PartContext &ctx, const Ooxml::XmlElement *container)
{
    CellBorder border;
    if (container == nullptr) {
        return border;
    }
    const Ooxml::XmlElement *ln = container->name == u"ln" ? container : container->child(nsA(), u"ln");
    if (ln != nullptr) {
        const ResolvedLine line = lineFrom(ctx, ln);
        border.set = true;
        border.color = line.colorSet ? line.color : QColor(Qt::black);
        border.width = line.width;
        border.dash = line.style.dash;
    } else if (const Ooxml::XmlElement *ref = container->child(nsA(), u"lnRef")) {
        const ResolvedLine line = lineFromReference(ctx, ref);
        border.set = line.present;
        border.color = line.color;
        border.width = line.width;
        border.dash = line.style.dash;
    }
    return border;
}

TableRegion regionFrom(const PartContext &ctx, const Ooxml::XmlElement *region)
{
    TableRegion out;
    if (region == nullptr) {
        return out;
    }
    if (const Ooxml::XmlElement *text = region->child(nsA(), u"tcTxStyle")) {
        out.bold = Ooxml::Units::parseOnOff(text->attr(u"b"), false);
        QColor c = childColor(ctx, text);
        if (!c.isValid()) {
            c = childColor(ctx, text->child(nsA(), u"fontRef"));
        }
        out.text = c;
    }
    if (const Ooxml::XmlElement *cell = region->child(nsA(), u"tcStyle")) {
        if (const Ooxml::XmlElement *borders = cell->child(nsA(), u"tcBdr")) {
            out.insideH = borderFrom(ctx, borders->child(nsA(), u"insideH"));
            out.insideV = borderFrom(ctx, borders->child(nsA(), u"insideV"));
            out.outer = borderFrom(ctx, borders->child(nsA(), u"top"));
            if (!out.outer.set) {
                out.outer = borderFrom(ctx, borders->child(nsA(), u"left"));
            }
        }
        if (const Ooxml::XmlElement *fill = cell->child(nsA(), u"fill")) {
            const ResolvedFill resolved = fillFrom(ctx, fill);
            if (resolved.present && resolved.plain.alpha() > 0) {
                out.fill = resolved.plain;
            }
        } else if (const Ooxml::XmlElement *ref = cell->child(nsA(), u"fillRef")) {
            const ResolvedFill resolved = fillFromReference(ctx, ref);
            if (resolved.present && resolved.plain.alpha() > 0) {
                out.fill = resolved.plain;
            }
        }
    }
    return out;
}

QColor scheme(const PartContext &ctx, const QString &slot, qreal tint = 1.0)
{
    Ooxml::XmlElement e(nsA(), QStringLiteral("schemeClr"));
    e.setAttribute(QString(), QStringLiteral("val"), slot);
    const QColor c = color(ctx, &e);
    return tint < 1.0 ? eightBit(Ooxml::applyTint(c, tint)) : c;
}

// PowerPoint's default table style, for producers that name it without
// writing its definition.
TableLook mediumStyle2(const PartContext &ctx)
{
    TableLook look;
    look.known = true;
    const QColor white = scheme(ctx, QStringLiteral("lt1"));
    const CellBorder thin{true, white, 1.0, QString()};
    look.whole.fill = scheme(ctx, QStringLiteral("accent1"), 0.2);
    look.whole.text = scheme(ctx, QStringLiteral("dk1"));
    look.whole.insideH = look.whole.insideV = look.whole.outer = thin;
    look.band1H.fill = scheme(ctx, QStringLiteral("accent1"), 0.4);
    look.band1V.fill = look.band1H.fill;
    for (TableRegion *region : {&look.firstRow, &look.lastRow, &look.firstCol, &look.lastCol}) {
        region->fill = scheme(ctx, QStringLiteral("accent1"));
        region->text = white;
        region->bold = true;
    }
    return look;
}

TableLook resolveLook(const ShapeScope &scope, const QString &styleId)
{
    const Ooxml::XmlElement *root = scope.tableStyles != nullptr ? scope.tableStyles->root.get() : nullptr;
    if (root != nullptr) {
        for (const Ooxml::XmlElement *style : root->elements(nsA(), u"tblStyle")) {
            if (style->attr(u"styleId") != styleId) {
                continue;
            }
            TableLook look;
            look.known = true;
            look.whole = regionFrom(scope.ctx, style->child(nsA(), u"wholeTbl"));
            look.band1H = regionFrom(scope.ctx, style->child(nsA(), u"band1H"));
            look.band2H = regionFrom(scope.ctx, style->child(nsA(), u"band2H"));
            look.band1V = regionFrom(scope.ctx, style->child(nsA(), u"band1V"));
            look.band2V = regionFrom(scope.ctx, style->child(nsA(), u"band2V"));
            look.firstRow = regionFrom(scope.ctx, style->child(nsA(), u"firstRow"));
            look.lastRow = regionFrom(scope.ctx, style->child(nsA(), u"lastRow"));
            look.firstCol = regionFrom(scope.ctx, style->child(nsA(), u"firstCol"));
            look.lastCol = regionFrom(scope.ctx, style->child(nsA(), u"lastCol"));
            return look;
        }
    }
    if (styleId.compare(kDefaultStyle, Qt::CaseInsensitive) == 0) {
        return mediumStyle2(scope.ctx);
    }
    return {};
}

void readCellProperties(const PartContext &ctx, const Ooxml::XmlElement *tcPr, TableCell &cell)
{
    if (tcPr == nullptr) {
        return;
    }
    const auto margin = [tcPr](const char16_t *name, qreal &into) {
        if (tcPr->hasAttribute(QString(), name)) {
            into = emuToPt(tcPr->attr(name));
        }
    };
    margin(u"marL", cell.marginLeft);
    margin(u"marR", cell.marginRight);
    margin(u"marT", cell.marginTop);
    margin(u"marB", cell.marginBottom);
    const QString anchor = tcPr->attr(u"anchor");
    cell.anchor = anchor == u"ctr" ? TextAnchor::Middle : anchor == u"b" ? TextAnchor::Bottom : TextAnchor::Top;
    cell.left = borderFrom(ctx, tcPr->child(nsA(), u"lnL"));
    cell.right = borderFrom(ctx, tcPr->child(nsA(), u"lnR"));
    cell.top = borderFrom(ctx, tcPr->child(nsA(), u"lnT"));
    cell.bottom = borderFrom(ctx, tcPr->child(nsA(), u"lnB"));
    const ResolvedFill fill = fillFrom(ctx, tcPr);
    if (fill.present) {
        if (fill.style.kind != FillKind::Plain) {
            cell.fillStyle = fill.style;
        } else if (fill.plain.alpha() == 0) {
            cell.noFill = true;
        } else {
            cell.fill = fill.plain;
        }
    }
}

} // namespace

Table readTable(const ShapeScope &scope, const Ooxml::XmlElement &tbl)
{
    Table table;
    if (const Ooxml::XmlElement *pr = tbl.child(nsA(), u"tblPr")) {
        table.firstRow = Ooxml::Units::parseOnOff(pr->attr(u"firstRow"), false);
        table.firstColumn = Ooxml::Units::parseOnOff(pr->attr(u"firstCol"), false);
        table.lastRow = Ooxml::Units::parseOnOff(pr->attr(u"lastRow"), false);
        table.lastColumn = Ooxml::Units::parseOnOff(pr->attr(u"lastCol"), false);
        table.bandRows = Ooxml::Units::parseOnOff(pr->attr(u"bandRow"), false);
        table.bandColumns = Ooxml::Units::parseOnOff(pr->attr(u"bandCol"), false);
        if (const Ooxml::XmlElement *id = pr->child(nsA(), u"tableStyleId")) {
            table.styleId = id->text().trimmed();
        }
    }
    if (const Ooxml::XmlElement *grid = tbl.child(nsA(), u"tblGrid")) {
        for (const Ooxml::XmlElement *column : grid->elements(nsA(), u"gridCol")) {
            table.columns.append(emuToPt(column->attr(u"w")));
        }
    }
    table.look = resolveLook(scope, table.styleId);
    const QList<const Ooxml::XmlElement *> rows = tbl.elements(nsA(), u"tr");
    for (const Ooxml::XmlElement *tr : rows) {
        TableRow row;
        row.height = emuToPt(tr->attr(u"h"));
        table.rows.append(row);
    }
    for (int r = 0; r < rows.size(); ++r) {
        int c = 0;
        for (const Ooxml::XmlElement *tc : rows.at(r)->elements(nsA(), u"tc")) {
            TableCell cell;
            cell.gridSpan = qMax(1, tc->attr(u"gridSpan", QStringLiteral("1")).toInt());
            cell.rowSpan = qMax(1, tc->attr(u"rowSpan", QStringLiteral("1")).toInt());
            cell.hMerge = Ooxml::Units::parseOnOff(tc->attr(u"hMerge"), false);
            cell.vMerge = Ooxml::Units::parseOnOff(tc->attr(u"vMerge"), false);
            readCellProperties(scope.ctx, tc->child(nsA(), u"tcPr"), cell);
            // The table style's text colour and weight sit between the
            // deck's default text style and the cell's own formatting.
            const TableRegion region = table.regionAt(r, c);
            RunProps regionRun;
            if (region.text.isValid()) {
                regionRun.color = region.text;
            }
            if (region.bold) {
                regionRun.bold = true;
            }
            const ListStyle inherited = scope.styles != nullptr ? scope.styles->other : ListStyle();
            cell.paragraphs = readTextBody(scope.ctx, tc->child(nsA(), u"txBody"), inherited, regionRun);
            table.rows[r].cells.append(cell);
            ++c;
        }
    }
    // M67 (D-076): every row and column is its own source row / column.
    for (int r = 0; r < table.rows.size(); ++r) {
        table.origin.rows.append(r);
    }
    for (int c = 0; c < table.columns.size(); ++c) {
        table.origin.columns.append(c);
    }
    table.origin.sourceRows = int(table.rows.size());
    table.origin.sourceColumns = int(table.columns.size());
    return table;
}

} // namespace QindaOffice::Show::Pptx

namespace QindaOffice::Show {

NewTableStyle newTableStyle(const Presentation &deck, const Slide &slide)
{
    using namespace Pptx;
    NewTableStyle out;
    out.styleId = kDefaultStyle;
    Pptx::ThemeContext theme;
    theme.theme = Ooxml::defaultOfficeTheme();
    Pptx::TableStyles styles;
    PartContext ctx;
    const DeckSource *source = deck.origin.get();
    if (source != nullptr && source->package) {
        const auto &package = source->package;
        ctx.package = package;
        if (const auto rel = package->firstOfType(source->presentationPart, QString::fromUtf16(Ooxml::RelType::TableStyles))) {
            styles.root = package->xml(package->targetPart(source->presentationPart, *rel));
            const QString def = styles.root ? styles.root->attr(u"def") : QString();
            if (!def.isEmpty()) {
                out.styleId = def;
            }
        }
        const SlideMaster *master = deck.masterFor(slide);
        const MasterSource *masterSource = master != nullptr ? master->origin.get() : nullptr;
        if (masterSource != nullptr) {
            ctx.part = masterSource->part;
            if (const auto rel = package->firstOfType(masterSource->part, QString::fromUtf16(Ooxml::RelType::Theme))) {
                const Ooxml::XmlElementPtr root = package->xml(package->targetPart(masterSource->part, *rel));
                if (root) {
                    theme = parseThemeContext(root.get());
                }
            }
            const Ooxml::XmlElementPtr masterRoot = package->xml(masterSource->part);
            if (const Ooxml::XmlElement *map = masterRoot ? masterRoot->child(nsP(), u"clrMap") : nullptr) {
                for (const Ooxml::XmlAttribute &a : map->attributes) {
                    if (a.ns.isEmpty()) {
                        ctx.map.insert(a.name, a.value);
                    }
                }
            }
        }
    }
    ctx.theme = &theme;
    ShapeScope scope;
    scope.ctx = ctx;
    scope.tableStyles = &styles;
    out.look = resolveLook(scope, out.styleId);
    if (!out.look.known) {
        // A default the deck names but does not define and PowerPoint does
        // not ship: fall back to PowerPoint's own default.
        out.styleId = kDefaultStyle;
        out.look = resolveLook(scope, out.styleId);
    }
    if (theme.theme.minorLatin.isEmpty()) {
        out.fontFamily = Ooxml::defaultOfficeTheme().minorLatin;
    } else {
        out.fontFamily = theme.theme.minorLatin;
    }
    // The deck's "other" text: dk1 through the colour map, as tx1.
    Ooxml::XmlElement tx1(nsA(), QStringLiteral("schemeClr"));
    tx1.setAttribute(QString(), QStringLiteral("val"), QStringLiteral("tx1"));
    out.textColor = Pptx::color(ctx, &tx1);
    return out;
}

} // namespace QindaOffice::Show
