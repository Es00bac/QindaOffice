// SPDX-License-Identifier: GPL-3.0-or-later
// The table writer's property re-derivations (M70, D-079): w:tblPr's
// alignment, borders, preferred width, indent, layout and default cell
// margins; w:trPr's height and "can't split"; w:tcPr's borders, margins
// and width. Each value is compared with the import snapshot
// (docx_reader_table.cpp); an unchanged one keeps its source markup
// (D-068 rule 3), and a table or cell Write made gets what Word writes.

#include "docx_borders.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QTextTable>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// Rewrites `parent`'s `name` (w:tblBorders, w:tcBorders) to `now`, keeping
// the source element of every side still equal to `then`.
void rewriteBorders(XmlElement &parent, const QString &name, const QString &now, const QString &then)
{
    const XmlElement *existing = parent.child(kW, name);
    const XmlElementPtr source = existing != nullptr ? existing->clone() : nullptr;
    parent.removeChildren(kW, name);
    const WordBorders sides = parseBordersSpec(now);
    if (!sides.isEmpty()) {
        writeBorders(wChild(parent, name), sides, parseBordersSpec(then), source.get());
    }
}

void rewriteMargins(XmlElement &parent, const QString &name, const QString &spec)
{
    parent.removeChildren(kW, name);
    const CellMargins margins = parseMarginsSpec(spec);
    if (!margins.isEmpty()) {
        writeMargins(wChild(parent, name), margins);
    }
}

} // namespace

void writeTableProperties(XmlElement &tblPr, const QTextTableFormat &tf, const QVariantMap &snapshot, bool imported)
{
    auto changed = [&](const char *key, const QVariant &now) {
        return !imported || snapshot.value(QString::fromLatin1(key)) != now;
    };
    if (changed("align", int(tf.alignment()))) {
        tblPr.removeChildren(kW, u"jc");
        const Qt::Alignment a = tf.alignment() & Qt::AlignHorizontal_Mask;
        if (a == Qt::AlignHCenter || a == Qt::AlignRight || imported) {
            setChildVal(tblPr, QStringLiteral("jc"),
                        a == Qt::AlignHCenter ? QStringLiteral("center")
                            : a == Qt::AlignRight ? QStringLiteral("right")
                                                  : QStringLiteral("left"));
        }
    }
    if (tf.hasProperty(kTableBorders) && changed("borders", tf.stringProperty(kTableBorders))) {
        rewriteBorders(tblPr, QStringLiteral("tblBorders"), tf.stringProperty(kTableBorders),
                       snapshot.value(QStringLiteral("borders")).toString());
    }
    if (tf.hasProperty(kTableWidth) && changed("width", tf.stringProperty(kTableWidth))) {
        writeWidth(wChild(tblPr, QStringLiteral("tblW")), parseWidthSpec(tf.stringProperty(kTableWidth)));
    }
    const int indent = tf.hasProperty(kTableIndent) ? tf.intProperty(kTableIndent) : -1;
    if (indent >= 0 && changed("indent", indent)) {
        tblPr.removeChildren(kW, u"tblInd");
        if (indent > 0 || imported) {
            XmlElement &ind = wChild(tblPr, QStringLiteral("tblInd"));
            ind.setAttribute(kW, QStringLiteral("w"), QString::number(indent));
            ind.setAttribute(kW, QStringLiteral("type"), QStringLiteral("dxa"));
        }
    }
    if (tf.hasProperty(kTableLayout) && changed("layout", tf.stringProperty(kTableLayout))) {
        tblPr.removeChildren(kW, u"tblLayout");
        if (tf.stringProperty(kTableLayout) == u"fixed") {
            wChild(tblPr, QStringLiteral("tblLayout")).setAttribute(kW, QStringLiteral("type"), QStringLiteral("fixed"));
        }
    }
    if (tf.hasProperty(kTableCellMargins) && changed("margins", tf.stringProperty(kTableCellMargins))) {
        rewriteMargins(tblPr, QStringLiteral("tblCellMar"), tf.stringProperty(kTableCellMargins));
    }
}

void writeRowProperties(XmlElement &trPr, const QTextCharFormat &anchor)
{
    const QVariantMap snapshot = anchor.property(kCellSnapshot).toMap();
    const bool imported = snapshot.contains(QStringLiteral("rowh"));
    const int height = qRound(Units::pxToTwips(anchor.doubleProperty(kRowHeight)));
    const QString rule = anchor.stringProperty(kRowHeightRule);
    if (!imported || snapshot.value(QStringLiteral("rowh")).toInt() != height
        || snapshot.value(QStringLiteral("hrule")).toString() != rule) {
        if (height > 0) {
            XmlElement &h = wChild(trPr, QStringLiteral("trHeight"));
            h.attributes.clear();
            h.setAttribute(kW, QStringLiteral("val"), QString::number(height));
            if (rule == u"exact") {
                h.setAttribute(kW, QStringLiteral("hRule"), QStringLiteral("exact"));
            }
        } else if (imported) {
            trPr.removeChildren(kW, u"trHeight");
        }
    }
    const bool cantSplit = anchor.boolProperty(kRowCantSplit);
    if (!imported || snapshot.value(QStringLiteral("cantSplit")).toBool() != cantSplit) {
        trPr.removeChildren(kW, u"cantSplit");
        if (cantSplit) {
            wChild(trPr, QStringLiteral("cantSplit"));
        }
    }
}

void writeCellProperties(XmlElement &tcPr, const QTextTableCellFormat &cf, bool cellImported)
{
    const QVariantMap snapshot = cf.property(kCellSnapshot).toMap();
    if (cf.hasProperty(kCellBorders)) {
        const QString now = cf.stringProperty(kCellBorders);
        if (!cellImported || snapshot.value(QStringLiteral("borders")).toString() != now) {
            rewriteBorders(tcPr, QStringLiteral("tcBorders"), now, snapshot.value(QStringLiteral("borders")).toString());
        }
    }
    if (cf.hasProperty(kCellMargins)) {
        const QString now = cf.stringProperty(kCellMargins);
        if (!cellImported || snapshot.value(QStringLiteral("margins")).toString() != now) {
            rewriteMargins(tcPr, QStringLiteral("tcMar"), now);
        }
    }
}

void writeCellWidth(XmlElement &tcPr, const QTextTable *table, int column, int span, const QList<qreal> &widthsPx)
{
    const QList<QTextLength> constraints = table->format().columnWidthConstraints();
    bool variable = true;
    bool percent = true;
    qreal pct = 0;
    qreal px = 0;
    for (int k = column; k < column + span && k < widthsPx.size(); ++k) {
        const QTextLength l = k < constraints.size() ? constraints.at(k) : QTextLength();
        variable = variable && l.type() == QTextLength::VariableLength;
        percent = percent && l.type() == QTextLength::PercentageLength;
        pct += l.type() == QTextLength::PercentageLength ? l.rawValue() : 0;
        px += widthsPx.at(k);
    }
    // AutoFit to contents leaves no preferred width; AutoFit to window
    // gives each cell its share of the table (fiftieths of a percent).
    const WordWidth width = variable ? WordWidth{QStringLiteral("auto"), 0}
        : percent                    ? WordWidth{QStringLiteral("pct"), qRound(pct * 50)}
                                     : WordWidth{QStringLiteral("dxa"), qRound(Units::pxToTwips(px))};
    writeWidth(wChild(tcPr, QStringLiteral("tcW")), width);
}

} // namespace QindaOffice::Docx::detail
