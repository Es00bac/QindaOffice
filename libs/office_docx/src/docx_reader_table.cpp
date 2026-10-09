// SPDX-License-Identifier: GPL-3.0-or-later
// Tables of a .docx body: grid, merges, widths, borders, shading, padding
// and the table style's conditional formats (header row, banding, …).

#include "docx_borders.h"
#include "docx_format.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QSet>
#include <QTextTable>

#include <utility>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

struct CellSlot {
    const XmlElement *tc = nullptr;
    const XmlElement *tr = nullptr;
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
};

struct Look {
    bool firstRow = true;
    bool lastRow = false;
    bool firstColumn = true;
    bool lastColumn = false;
    bool hBand = true;
    bool vBand = false;
};

Look lookOf(const XmlElement *tblPr)
{
    Look look;
    const XmlElement *e = tblPr != nullptr ? tblPr->child(kW, u"tblLook") : nullptr;
    if (e == nullptr) {
        return look;
    }
    const QString val = wAttr(e, u"val");
    if (!val.isEmpty() && wAttr(e, u"firstRow").isEmpty()) {
        // Legacy bitmask (ECMA-376 2006): 0x20 first row, 0x40 last row,
        // 0x80 first column, 0x100 last column, 0x200 no H band, 0x400 no V band.
        const uint bits = val.toUInt(nullptr, 16);
        look.firstRow = bits & 0x20;
        look.lastRow = bits & 0x40;
        look.firstColumn = bits & 0x80;
        look.lastColumn = bits & 0x100;
        look.hBand = !(bits & 0x200);
        look.vBand = !(bits & 0x400);
        return look;
    }
    look.firstRow = Units::parseOnOff(wAttr(e, u"firstRow"), false);
    look.lastRow = Units::parseOnOff(wAttr(e, u"lastRow"), false);
    look.firstColumn = Units::parseOnOff(wAttr(e, u"firstColumn"), false);
    look.lastColumn = Units::parseOnOff(wAttr(e, u"lastColumn"), false);
    look.hBand = !Units::parseOnOff(wAttr(e, u"noHBand"), false);
    look.vBand = !Units::parseOnOff(wAttr(e, u"noVBand"), false);
    return look;
}

// Rows of a table, looking through row-level content controls.
void collectRows(const XmlElement &parent, QList<const XmlElement *> &rows)
{
    for (const XmlElement *e : parent.elements()) {
        if (e->ns != kW) {
            continue;
        }
        if (e->name == u"tr") {
            rows.append(e);
        } else if (e->name == u"sdt") {
            if (const XmlElement *content = e->child(kW, u"sdtContent")) {
                collectRows(*content, rows);
            }
        } else if (e->name == u"customXml") {
            collectRows(*e, rows);
        }
    }
}

void collectCells(const XmlElement &row, QList<const XmlElement *> &cells)
{
    for (const XmlElement *e : row.elements()) {
        if (e->ns != kW) {
            continue;
        }
        if (e->name == u"tc") {
            cells.append(e);
        } else if (e->name == u"sdt") {
            if (const XmlElement *content = e->child(kW, u"sdtContent")) {
                collectCells(*content, cells);
            }
        } else if (e->name == u"customXml") {
            collectCells(*e, cells);
        }
    }
}

struct Border {
    qreal width = 0; // px; 0 = none
    QTextFrameFormat::BorderStyle style = QTextFrameFormat::BorderStyle_Solid;
    QColor color = Qt::black;
};

Border borderOf(const XmlElement *side, const Ooxml::Theme &theme)
{
    Border b;
    if (side == nullptr) {
        return b;
    }
    const QString val = wAttr(side, u"val", QStringLiteral("single"));
    if (val == u"nil" || val == u"none") {
        return b;
    }
    // w:sz is eighths of a point.
    b.width = qMax<qreal>(0.5, Units::ptToPx(wAttr(side, u"sz", QStringLiteral("4")).toDouble() / 8.0));
    if (val == u"double" || val.startsWith(u"triple") || val.startsWith(u"thinThick") || val.startsWith(u"thickThin")) {
        b.style = QTextFrameFormat::BorderStyle_Double;
        b.width = qMax<qreal>(b.width, 2.5);
    } else if (val.startsWith(u"dot")) {
        b.style = val.startsWith(u"dotDash") ? QTextFrameFormat::BorderStyle_DotDash
                                             : QTextFrameFormat::BorderStyle_Dotted;
    } else if (val.startsWith(u"dash")) {
        b.style = QTextFrameFormat::BorderStyle_Dashed;
    }
    const QColor c = wordColor(side, theme, u"color");
    if (c.isValid()) {
        b.color = c;
    }
    return b;
}

const XmlElement *side(const XmlElement *borders, QStringView a, QStringView b)
{
    if (borders == nullptr) {
        return nullptr;
    }
    const XmlElement *e = borders->child(kW, a);
    return e != nullptr ? e : borders->child(kW, b);
}

qreal marginOf(const XmlElement *margins, QStringView a, QStringView b, qreal fallbackTwips)
{
    const XmlElement *e = side(margins, a, b);
    if (e == nullptr || (wAttr(e, u"type") != u"dxa" && !wAttr(e, u"type").isEmpty())) {
        return Units::twipsToPx(fallbackTwips);
    }
    return Units::twipsToPx(Units::parseTwips(wAttr(e, u"w")));
}

} // namespace

void BodyReader::readTable(const XmlElement &tbl, QTextCursor &cursor, ContainerState &state)
{
    const StyleSheet &styles = m_env.styles;
    const XmlElement *tblPr = tbl.child(kW, u"tblPr");
    QString styleId = childVal(tblPr, u"tblStyle");
    if (styles.style(styleId) == nullptr) {
        styleId = styles.defaultTableStyle();
    }
    const TableStyleLayer whole = styles.tableLayer(styleId);
    XmlElementPtr effTblPr = mergeProperties(whole.tblPr.get(), tblPr);
    if (!effTblPr) {
        effTblPr = std::make_shared<XmlElement>(kW, QStringLiteral("tblPr"));
    }
    const Look look = lookOf(effTblPr.get());

    QList<qreal> grid;
    if (const XmlElement *tblGrid = tbl.child(kW, u"tblGrid")) {
        for (const XmlElement *col : tblGrid->elements(kW, u"gridCol")) {
            grid.append(Units::twipsToPx(Units::parseTwips(wAttr(col, u"w"))));
        }
    }
    QList<const XmlElement *> rows;
    collectRows(tbl, rows);
    if (rows.isEmpty()) {
        return;
    }
    // Lay the cells on the grid: gridBefore skips, gridSpan widens, vMerge
    // continues the cell above.
    QList<CellSlot> cellSlots;
    QHash<QPair<int, int>, int> owner; // (row, column) → slot index
    int columns = int(grid.size());
    for (int r = 0; r < rows.size(); ++r) {
        const XmlElement *trPr = rows[r]->child(kW, u"trPr");
        int column = childVal(trPr, u"gridBefore", QStringLiteral("0")).toInt();
        QList<const XmlElement *> cells;
        collectCells(*rows[r], cells);
        for (const XmlElement *tc : cells) {
            const XmlElement *tcPr = tc->child(kW, u"tcPr");
            const int span = qMax(1, childVal(tcPr, u"gridSpan", QStringLiteral("1")).toInt());
            const XmlElement *vMerge = tcPr != nullptr ? tcPr->child(kW, u"vMerge") : nullptr;
            const bool continues = vMerge != nullptr && wAttr(vMerge, u"val") != u"restart";
            const auto above = owner.constFind({r - 1, column});
            if (continues && above != owner.constEnd()) {
                CellSlot &top = cellSlots[above.value()];
                top.rowSpan = r - top.row + 1;
                for (int c = column; c < column + span; ++c) {
                    owner.insert({r, c}, above.value());
                }
            } else {
                cellSlots.append({tc, rows[r], r, column, 1, span});
                for (int c = column; c < column + span; ++c) {
                    owner.insert({r, c}, int(cellSlots.size()) - 1);
                }
            }
            column += span;
        }
        columns = qMax(columns, column);
    }
    if (columns == 0) {
        return;
    }
    while (grid.size() < columns) {
        grid.append(grid.isEmpty() ? 96.0 : grid.last());
    }

    QTextTableFormat tf;
    tf.setBorderCollapse(true);
    tf.setBorder(0);
    tf.setCellSpacing(0);
    tf.setCellPadding(0);
    QList<QTextLength> widths;
    for (int c = 0; c < columns; ++c) {
        widths.append(QTextLength(QTextLength::FixedLength, grid[c]));
    }
    tf.setColumnWidthConstraints(widths);
    const QString jc = childVal(effTblPr.get(), u"jc");
    tf.setAlignment(jc == u"center" ? Qt::AlignHCenter : (jc == u"right" || jc == u"end") ? Qt::AlignRight
                                                                                          : Qt::AlignLeft);
    if (const XmlElement *ind = effTblPr->child(kW, u"tblInd")) {
        if (wAttr(ind, u"type", QStringLiteral("dxa")) == u"dxa") {
            tf.setLeftMargin(qMax<qreal>(0, Units::twipsToPx(Units::parseTwips(wAttr(ind, u"w")))));
        }
    }
    int headerRows = 0;
    while (headerRows < rows.size() && onOff(rows[headerRows]->child(kW, u"trPr"), u"tblHeader")) {
        ++headerRows;
    }
    tf.setHeaderRowCount(headerRows);
    if (state.tableTopMargin > 0) {
        tf.setTopMargin(state.tableTopMargin); // see ContainerState::afterTable
        state.tableTopMargin = 0;
    }
    if (tblPr != nullptr) {
        tf.setProperty(kTableXml, Ooxml::serializeFragment(*tblPr));
    }
    // M70 (D-079): the Word values the table commands edit (docx_table_format.h).
    tf.setProperty(kTableBorders, bordersSpec(readBorders(effTblPr->child(kW, u"tblBorders"), m_env.theme)));
    if (const XmlElement *w = effTblPr->child(kW, u"tblW")) {
        tf.setProperty(kTableWidth, widthSpec(readWidth(w)));
    }
    if (const XmlElement *ind = effTblPr->child(kW, u"tblInd"); ind != nullptr && wAttr(ind, u"type", QStringLiteral("dxa")) == u"dxa") {
        tf.setProperty(kTableIndent, qRound(Units::parseTwips(wAttr(ind, u"w"))));
    }
    tf.setProperty(kTableLayout, wAttr(effTblPr->child(kW, u"tblLayout"), u"type", QStringLiteral("autofit")));
    tf.setProperty(kTableCellMargins, marginsSpec(readMargins(effTblPr->child(kW, u"tblCellMar"))));
    if (const XmlElement *tblGrid = tbl.child(kW, u"tblGrid")) {
        tf.setProperty(kTableGridXml, Ooxml::serializeFragment(*tblGrid));
    }

    if (!m_blockControls.isEmpty()) {
        tf.setProperty(kBlockControls, m_blockControls); // a block-level control around the table
    }

    QTextTable *table = cursor.insertTable(int(rows.size()), columns, tf);
    QTextTableFormat stored = table->format();
    QStringList gridTwips;
    for (const qreal w : std::as_const(grid)) {
        gridTwips.append(QString::number(qRound(Units::pxToTwips(w))));
    }
    stored.setProperty(kTableSnapshot, QVariantMap{{QStringLiteral("columns"), columns},
                                                   {QStringLiteral("align"), int(stored.alignment())},
                                                   {QStringLiteral("header"), headerRows},
                                                   {QStringLiteral("borders"), stored.stringProperty(kTableBorders)},
                                                   {QStringLiteral("width"), stored.stringProperty(kTableWidth)},
                                                   {QStringLiteral("indent"), stored.hasProperty(kTableIndent) ? stored.intProperty(kTableIndent) : -1},
                                                   {QStringLiteral("layout"), stored.stringProperty(kTableLayout)},
                                                   {QStringLiteral("margins"), stored.stringProperty(kTableCellMargins)},
                                                   {QStringLiteral("grid"), gridTwips.join(QLatin1Char(','))}});
    table->setFormat(stored);
    // The cells' paragraphs belong to the table, not to the control around it.
    const QStringList outerControls = std::exchange(m_blockControls, QStringList());
    for (const CellSlot &s : std::as_const(cellSlots)) {
        if (s.rowSpan > 1 || s.columnSpan > 1) {
            table->mergeCells(s.row, s.column, s.rowSpan, s.columnSpan);
        }
    }

    const XmlElement *tableBorders = effTblPr->child(kW, u"tblBorders");
    QSet<int> anchored; // rows whose first cell carries their properties
    const int rowBand = qMax(1, childVal(effTblPr.get(), u"tblStyleRowBandSize", QStringLiteral("1")).toInt());
    const int colBand = qMax(1, childVal(effTblPr.get(), u"tblStyleColBandSize", QStringLiteral("1")).toInt());
    const int lastRow = int(rows.size()) - 1;
    for (const CellSlot &s : std::as_const(cellSlots)) {
        // Conditional layers in ECMA order: bands, first/last row,
        // first/last column, corners (later wins).
        TableStyleLayer layer = whole;
        auto apply = [&](const QString &type) {
            const TableStyleLayer c = styles.tableCondition(styleId, type);
            layer.pPr = mergeProperties(layer.pPr.get(), c.pPr.get());
            layer.rPr = mergeProperties(layer.rPr.get(), c.rPr.get());
            layer.tcPr = mergeProperties(layer.tcPr.get(), c.tcPr.get());
            layer.tblPr = mergeProperties(layer.tblPr.get(), c.tblPr.get());
        };
        const bool isFirstRow = look.firstRow && s.row == 0;
        const bool isLastRow = look.lastRow && s.row + s.rowSpan - 1 == lastRow;
        const bool isFirstCol = look.firstColumn && s.column == 0;
        const bool isLastCol = look.lastColumn && s.column + s.columnSpan == columns;
        if (look.vBand && !isFirstCol && !isLastCol) {
            const int index = (s.column - (look.firstColumn ? 1 : 0)) / colBand;
            apply(index % 2 == 0 ? QStringLiteral("band1Vert") : QStringLiteral("band2Vert"));
        }
        if (look.hBand && !isFirstRow && !isLastRow) {
            const int index = (s.row - (look.firstRow ? 1 : 0)) / rowBand;
            apply(index % 2 == 0 ? QStringLiteral("band1Horz") : QStringLiteral("band2Horz"));
        }
        if (isFirstRow) {
            apply(QStringLiteral("firstRow"));
        }
        if (isLastRow) {
            apply(QStringLiteral("lastRow"));
        }
        if (isFirstCol) {
            apply(QStringLiteral("firstCol"));
        }
        if (isLastCol) {
            apply(QStringLiteral("lastCol"));
        }
        if (isFirstRow && isFirstCol) {
            apply(QStringLiteral("nwCell"));
        }
        if (isFirstRow && isLastCol) {
            apply(QStringLiteral("neCell"));
        }
        if (isLastRow && isFirstCol) {
            apply(QStringLiteral("swCell"));
        }
        if (isLastRow && isLastCol) {
            apply(QStringLiteral("seCell"));
        }
        const XmlElement *tcPr = s.tc->child(kW, u"tcPr");
        XmlElementPtr effTcPr = mergeProperties(layer.tcPr.get(), tcPr);
        if (!effTcPr) {
            effTcPr = std::make_shared<XmlElement>(kW, QStringLiteral("tcPr"));
        }

        QTextTableCell cell = table->cellAt(s.row, s.column);
        QTextTableCellFormat cf = cell.format().toTableCellFormat();
        const QColor fill = shadingColor(effTcPr->child(kW, u"shd"), m_env.theme);
        if (fill.isValid()) {
            cf.setBackground(fill);
        }
        const QString vAlign = childVal(effTcPr.get(), u"vAlign");
        cf.setVerticalAlignment(vAlign == u"center" ? QTextCharFormat::AlignMiddle
                                    : vAlign == u"bottom" ? QTextCharFormat::AlignBottom
                                                          : QTextCharFormat::AlignTop);
        const XmlElement *tableMargins = effTblPr->child(kW, u"tblCellMar");
        const XmlElement *cellMargins = effTcPr->child(kW, u"tcMar");
        auto margin = [&](QStringView a, QStringView b, qreal fallbackTwips) {
            const qreal table = marginOf(tableMargins, a, b, fallbackTwips);
            return cellMargins != nullptr && side(cellMargins, a, b) != nullptr
                ? marginOf(cellMargins, a, b, fallbackTwips)
                : table;
        };
        cf.setLeftPadding(margin(u"left", u"start", 108));
        cf.setRightPadding(margin(u"right", u"end", 108));
        cf.setTopPadding(margin(u"top", u"top", 0));
        cf.setBottomPadding(margin(u"bottom", u"bottom", 0));
        // Borders: the table's outer or inside lines by position, then the
        // conditional layer's and the cell's own w:tcBorders.
        const XmlElement *condBorders = layer.tblPr ? layer.tblPr->child(kW, u"tblBorders") : nullptr;
        const XmlElement *cellBorders = effTcPr->child(kW, u"tcBorders");
        auto pick = [&](QStringView outerA, QStringView outerB, QStringView inside, bool outer) {
            const XmlElement *e = side(cellBorders, outerA, outerB);
            if (e == nullptr) {
                e = outer ? side(condBorders, outerA, outerB) : side(condBorders, inside, inside);
            }
            if (e == nullptr) {
                e = outer ? side(tableBorders, outerA, outerB) : side(tableBorders, inside, inside);
            }
            return borderOf(e, m_env.theme);
        };
        const Border top = pick(u"top", u"top", u"insideH", s.row == 0);
        const Border bottom = pick(u"bottom", u"bottom", u"insideH", s.row + s.rowSpan - 1 == lastRow);
        const Border left = pick(u"left", u"start", u"insideV", s.column == 0);
        const Border right = pick(u"right", u"end", u"insideV", s.column + s.columnSpan == columns);
        cf.setTopBorder(top.width);
        cf.setTopBorderStyle(top.style);
        cf.setTopBorderBrush(top.color);
        cf.setBottomBorder(bottom.width);
        cf.setBottomBorderStyle(bottom.style);
        cf.setBottomBorderBrush(bottom.color);
        cf.setLeftBorder(left.width);
        cf.setLeftBorderStyle(left.style);
        cf.setLeftBorderBrush(left.color);
        cf.setRightBorder(right.width);
        cf.setRightBorderStyle(right.style);
        cf.setRightBorderBrush(right.color);
        if (tcPr != nullptr) {
            cf.setProperty(kCellXml, Ooxml::serializeFragment(*tcPr));
        }
        // M70 (D-079): the cell's own borders and margins, as Word wrote them.
        cf.setProperty(kCellBorders,
                       bordersSpec(readBorders(tcPr != nullptr ? tcPr->child(kW, u"tcBorders") : nullptr, m_env.theme)));
        cf.setProperty(kCellMargins, marginsSpec(readMargins(tcPr != nullptr ? tcPr->child(kW, u"tcMar") : nullptr)));
        QVariantMap snapshot{
            {QStringLiteral("bg"), cf.hasProperty(QTextFormat::BackgroundBrush) ? cf.background().color().name() : QString()},
            {QStringLiteral("valign"), int(cf.verticalAlignment())},
            {QStringLiteral("borders"), cf.stringProperty(kCellBorders)},
            {QStringLiteral("margins"), cf.stringProperty(kCellMargins)},
            {QStringLiteral("span"), s.columnSpan},
        };
        // The row's own properties ride on its first cell (docx_table_format.h
        // rowAnchor): the leftmost cell that starts in the row.
        if (!anchored.contains(s.row)) {
            anchored.insert(s.row);
            if (const XmlElement *trPr = s.tr->child(kW, u"trPr")) {
                cf.setProperty(kRowXml, Ooxml::serializeFragment(*trPr));
                if (const XmlElement *height = trPr->child(kW, u"trHeight")) {
                    const QString rule = wAttr(height, u"hRule", QStringLiteral("atLeast"));
                    // hRule auto means "as tall as the content": no minimum.
                    if (rule != u"auto" && s.rowSpan == 1) {
                        cf.setProperty(kRowHeight, Units::twipsToPx(Units::parseTwips(wAttr(height, u"val"))));
                        cf.setProperty(kRowHeightRule, rule == u"exact" ? QStringLiteral("exact") : QStringLiteral("atLeast"));
                    }
                }
                cf.setProperty(kRowCantSplit, onOff(trPr, u"cantSplit"));
            }
            snapshot.insert(QStringLiteral("rowh"), qRound(Units::pxToTwips(cf.doubleProperty(kRowHeight))));
            snapshot.insert(QStringLiteral("hrule"), cf.stringProperty(kRowHeightRule));
            snapshot.insert(QStringLiteral("cantSplit"), cf.boolProperty(kRowCantSplit));
        }
        cf.setProperty(kCellSnapshot, snapshot);
        cell.setFormat(cf);

        CellContext context{layer.pPr, layer.rPr};
        ContainerState cellState;
        cellState.cell = &context;
        cellState.fields = state.fields;
        QTextCursor cellCursor = cell.firstCursorPosition();
        readBlocks(*s.tc, cellCursor, cellState);
        state.fields = cellState.fields;
    }
    m_blockControls = outerControls;
    cursor = table->lastCursorPosition();
    cursor.movePosition(QTextCursor::NextBlock);
    state.blockFresh = true;
    state.afterTable = true;
    state.first = false;
    state.previousStyle.clear();
    state.previousContextual = false;
    state.previousAutoAfter = false;
    state.previousNumId = 0;
}

} // namespace QindaOffice::Docx::detail
