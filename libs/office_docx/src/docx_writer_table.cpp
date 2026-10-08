// SPDX-License-Identifier: GPL-3.0-or-later
// Tables of the .docx writer: grid, merges (w:gridSpan / w:vMerge), widths,
// borders, shading, cell alignment and header rows. Tables read from a
// .docx keep their w:tblPr / w:tblGrid / w:trPr / w:tcPr markup; merge
// structure and changed properties are re-derived from the QTextTable.

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QTextFrame>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb).mid(1).toUpper();
}

// A border side from a Qt cell/table border; sz is eighths of a point.
void setBorder(XmlElement &borders, const QString &side, qreal widthPx, const QBrush &brush,
               QTextFrameFormat::BorderStyle style)
{
    XmlElement &e = wChild(borders, side);
    e.attributes.clear();
    if (widthPx <= 0 || style == QTextFrameFormat::BorderStyle_None) {
        e.setAttribute(kW, QStringLiteral("val"), QStringLiteral("nil"));
        return;
    }
    const QString val = style == QTextFrameFormat::BorderStyle_Double ? QStringLiteral("double")
        : style == QTextFrameFormat::BorderStyle_Dashed               ? QStringLiteral("dashed")
        : style == QTextFrameFormat::BorderStyle_Dotted               ? QStringLiteral("dotted")
        : style == QTextFrameFormat::BorderStyle_DotDash              ? QStringLiteral("dotDash")
                                                                      : QStringLiteral("single");
    e.setAttribute(kW, QStringLiteral("val"), val);
    e.setAttribute(kW, QStringLiteral("sz"), QString::number(qBound(2, qRound(Units::pxToPt(widthPx) * 8), 96)));
    e.setAttribute(kW, QStringLiteral("space"), QStringLiteral("0"));
    const QColor c = brush.style() != Qt::NoBrush ? brush.color() : QColor(Qt::black);
    e.setAttribute(kW, QStringLiteral("color"), hex(c));
}

QList<qreal> columnWidths(const QTextTable *table, qreal contentWidth)
{
    const int columns = table->columns();
    const QList<QTextLength> constraints = table->format().columnWidthConstraints();
    QList<qreal> widths(columns, 0.0);
    qreal fixed = 0;
    int variable = 0;
    for (int c = 0; c < columns; ++c) {
        const QTextLength l = c < constraints.size() ? constraints.at(c) : QTextLength();
        if (l.type() == QTextLength::FixedLength) {
            widths[c] = l.rawValue();
        } else if (l.type() == QTextLength::PercentageLength) {
            widths[c] = contentWidth * l.rawValue() / 100.0;
        } else {
            ++variable;
            continue;
        }
        fixed += widths[c];
    }
    if (variable > 0) {
        const qreal share = qMax<qreal>(24.0, (contentWidth - fixed) / variable);
        for (int c = 0; c < columns; ++c) {
            if (widths[c] <= 0) {
                widths[c] = share;
            }
        }
    }
    return widths;
}

} // namespace

void BodyWriter::writeTable(const QTextTable *table, XmlElement &container)
{
    const QTextTableFormat tf = table->format();
    const int rows = table->rows();
    const int columns = table->columns();
    if (rows == 0 || columns == 0) {
        return;
    }
    XmlElement &tbl = container.appendElement(kW, QStringLiteral("tbl"));
    const QVariantMap snapshot = tf.property(kTableSnapshot).toMap();
    const bool imported = !snapshot.isEmpty();
    const qreal contentWidth = m_env.parts->layout.contentWidth();
    const QList<qreal> widths = columnWidths(table, contentWidth);

    XmlElementPtr tblPr = Ooxml::parseFragment(tf.stringProperty(kTableXml));
    if (!tblPr) {
        tblPr = std::make_shared<XmlElement>(kW, QStringLiteral("tblPr"));
    }
    if (!imported) {
        setChildVal(*tblPr, QStringLiteral("tblStyle"), QStringLiteral("TableGrid"));
        m_env.usedStyles.insert(QStringLiteral("TableGrid"));
        XmlElement &w = wChild(*tblPr, QStringLiteral("tblW"));
        const QTextLength width = tf.width();
        if (width.type() == QTextLength::PercentageLength) {
            w.setAttribute(kW, QStringLiteral("w"), QString::number(qRound(width.rawValue() * 50)));
            w.setAttribute(kW, QStringLiteral("type"), QStringLiteral("pct"));
        } else {
            w.setAttribute(kW, QStringLiteral("w"), QStringLiteral("0"));
            w.setAttribute(kW, QStringLiteral("type"), QStringLiteral("auto"));
        }
        XmlElement &borders = wChild(*tblPr, QStringLiteral("tblBorders"));
        for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
            setBorder(borders, QString::fromLatin1(side), tf.border(), tf.borderBrush(),
                      tf.border() > 0 ? QTextFrameFormat::BorderStyle_Solid : QTextFrameFormat::BorderStyle_None);
        }
        XmlElement &margins = wChild(*tblPr, QStringLiteral("tblCellMar"));
        for (const char *side : {"left", "right"}) {
            XmlElement &m = wChild(margins, QString::fromLatin1(side));
            m.setAttribute(kW, QStringLiteral("w"), twips(qMax<qreal>(tf.cellPadding(), 0)));
            m.setAttribute(kW, QStringLiteral("type"), QStringLiteral("dxa"));
        }
        XmlElement &look = wChild(*tblPr, QStringLiteral("tblLook"));
        look.setAttribute(kW, QStringLiteral("val"), QStringLiteral("04A0"));
        look.setAttribute(kW, QStringLiteral("firstRow"), QStringLiteral("1"));
        look.setAttribute(kW, QStringLiteral("lastRow"), QStringLiteral("0"));
        look.setAttribute(kW, QStringLiteral("firstColumn"), QStringLiteral("1"));
        look.setAttribute(kW, QStringLiteral("lastColumn"), QStringLiteral("0"));
        look.setAttribute(kW, QStringLiteral("noHBand"), QStringLiteral("0"));
        look.setAttribute(kW, QStringLiteral("noVBand"), QStringLiteral("1"));
    } else if (const XmlElement *style = tblPr->child(kW, u"tblStyle")) {
        m_env.usedStyles.insert(style->attr(u"val"));
    }
    if (!imported || snapshot.value(QStringLiteral("align")).toInt() != int(tf.alignment())) {
        tblPr->removeChildren(kW, u"jc");
        const Qt::Alignment a = tf.alignment() & Qt::AlignHorizontal_Mask;
        if (a == Qt::AlignHCenter || a == Qt::AlignRight || imported) {
            setChildVal(*tblPr, QStringLiteral("jc"),
                        a == Qt::AlignHCenter ? QStringLiteral("center")
                            : a == Qt::AlignRight ? QStringLiteral("right")
                                                  : QStringLiteral("left"));
        }
    }
    sortChildren(*tblPr);
    tbl.appendElement(tblPr);

    XmlElementPtr grid = Ooxml::parseFragment(tf.stringProperty(kTableGridXml));
    if (!grid || snapshot.value(QStringLiteral("columns")).toInt() != columns) {
        grid = std::make_shared<XmlElement>(kW, QStringLiteral("tblGrid"));
        for (const qreal w : widths) {
            grid->appendElement(kW, QStringLiteral("gridCol")).setAttribute(kW, QStringLiteral("w"), twips(w));
        }
    }
    tbl.appendElement(grid);

    const int headerRows = tf.headerRowCount();
    for (int r = 0; r < rows; ++r) {
        XmlElement &tr = tbl.appendElement(kW, QStringLiteral("tr"));
        const QTextTableCell first = table->cellAt(r, 0);
        XmlElementPtr trPr = first.row() == r ? Ooxml::parseFragment(first.format().stringProperty(kRowXml)) : nullptr;
        if (!trPr) {
            trPr = std::make_shared<XmlElement>(kW, QStringLiteral("trPr"));
        }
        // Every grid cell is written, so skipped leading/trailing columns
        // are no longer skipped.
        for (const char *name : {"gridBefore", "gridAfter", "wBefore", "wAfter"}) {
            trPr->removeChildren(kW, QString::fromLatin1(name));
        }
        trPr->removeChildren(kW, u"tblHeader");
        if (r < headerRows) {
            wChild(*trPr, QStringLiteral("tblHeader"));
        }
        sortChildren(*trPr);
        if (!trPr->children.isEmpty()) {
            tr.appendElement(trPr);
        }
        for (int c = 0; c < columns;) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (cell.column() < c) {
                // Covered by a merge that starts further left (already
                // written with its w:gridSpan).
                ++c;
                continue;
            }
            const int span = qMax(1, qMin(cell.columnSpan(), columns - c));
            qreal width = 0;
            for (int k = c; k < c + span && k < widths.size(); ++k) {
                width += widths.at(k);
            }
            XmlElement &tc = tr.appendElement(kW, QStringLiteral("tc"));
            const QTextTableCellFormat cf = cell.format().toTableCellFormat();
            XmlElementPtr tcPr = Ooxml::parseFragment(cf.stringProperty(kCellXml));
            const bool cellImported = cf.hasProperty(kCellSnapshot);
            if (!tcPr) {
                tcPr = std::make_shared<XmlElement>(kW, QStringLiteral("tcPr"));
            }
            XmlElement &w = wChild(*tcPr, QStringLiteral("tcW"));
            w.attributes.clear();
            w.setAttribute(kW, QStringLiteral("w"), twips(width));
            w.setAttribute(kW, QStringLiteral("type"), QStringLiteral("dxa"));
            tcPr->removeChildren(kW, u"gridSpan");
            tcPr->removeChildren(kW, u"hMerge");
            tcPr->removeChildren(kW, u"vMerge");
            if (span > 1) {
                setChildVal(*tcPr, QStringLiteral("gridSpan"), QString::number(span));
            }
            const bool continuation = cell.row() != r;
            if (continuation) {
                wChild(*tcPr, QStringLiteral("vMerge"));
            } else if (cell.rowSpan() > 1) {
                setChildVal(*tcPr, QStringLiteral("vMerge"), QStringLiteral("restart"));
            }
            const QVariantMap cellSnapshot = cf.property(kCellSnapshot).toMap();
            const QString bg = cf.hasProperty(QTextFormat::BackgroundBrush) ? cf.background().color().name() : QString();
            if (!cellImported || cellSnapshot.value(QStringLiteral("bg")).toString() != bg) {
                tcPr->removeChildren(kW, u"shd");
                if (!bg.isEmpty() || cellImported) {
                    XmlElement &shd = wChild(*tcPr, QStringLiteral("shd"));
                    shd.setAttribute(kW, QStringLiteral("val"), QStringLiteral("clear"));
                    shd.setAttribute(kW, QStringLiteral("color"), QStringLiteral("auto"));
                    shd.setAttribute(kW, QStringLiteral("fill"), bg.isEmpty() ? QStringLiteral("auto") : hex(cf.background().color()));
                }
            }
            if (!cellImported || cellSnapshot.value(QStringLiteral("valign")).toInt() != int(cf.verticalAlignment())) {
                tcPr->removeChildren(kW, u"vAlign");
                const auto v = cf.verticalAlignment();
                if (v == QTextCharFormat::AlignMiddle || v == QTextCharFormat::AlignBottom || cellImported) {
                    setChildVal(*tcPr, QStringLiteral("vAlign"),
                                v == QTextCharFormat::AlignMiddle   ? QStringLiteral("center")
                                    : v == QTextCharFormat::AlignBottom ? QStringLiteral("bottom")
                                                                        : QStringLiteral("top"));
                }
            }
            if (!cellImported && (cf.topBorder() > 0 || cf.leftBorder() > 0 || cf.bottomBorder() > 0 || cf.rightBorder() > 0)) {
                XmlElement &borders = wChild(*tcPr, QStringLiteral("tcBorders"));
                setBorder(borders, QStringLiteral("top"), cf.topBorder(), cf.topBorderBrush(), cf.topBorderStyle());
                setBorder(borders, QStringLiteral("left"), cf.leftBorder(), cf.leftBorderBrush(), cf.leftBorderStyle());
                setBorder(borders, QStringLiteral("bottom"), cf.bottomBorder(), cf.bottomBorderBrush(), cf.bottomBorderStyle());
                setBorder(borders, QStringLiteral("right"), cf.rightBorder(), cf.rightBorderBrush(), cf.rightBorderStyle());
            }
            sortChildren(*tcPr);
            tc.appendElement(tcPr);
            if (continuation) {
                tc.appendElement(kW, QStringLiteral("p"));
            } else {
                writeCell(cell, tc);
            }
            c += span;
        }
    }
}

void BodyWriter::writeCell(const QTextTableCell &cell, XmlElement &tc)
{
    QList<QTextFrame::iterator> items;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        items.append(it);
    }
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (QTextFrame *child = items.at(i).currentFrame()) {
            if (const auto *nested = qobject_cast<const QTextTable *>(child)) {
                writeTable(nested, tc);
            } else {
                writeFrame(child, tc);
            }
            continue;
        }
        writeBlock(items.at(i).currentBlock(), tc, i == items.size() - 1);
    }
    // A cell must end with a paragraph (ECMA-376 §17.4.66; Word refuses
    // the file otherwise).
    const QList<const XmlElement *> children = tc.elements();
    if (children.isEmpty() || children.last()->name != u"p") {
        tc.appendElement(kW, QStringLiteral("p"));
    }
}

} // namespace QindaOffice::Docx::detail
