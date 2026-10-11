// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_envelopes.h"

#include "docx_borders.h"
#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_table_format.h"
#include "ooxml_units.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

namespace QindaOffice::Docx {

namespace Units = Ooxml::Units;

namespace {

constexpr qreal kInch = 96.0;
constexpr qreal kMm = 96.0 / 25.4;
// Word's envelope margins and the label text indent (95 twips).
constexpr qreal kEnvelopeMargin = 0.4 * kInch;
const qreal kLabelIndent = Units::twipsToPx(95);

QString twipsText(qreal px)
{
    return QString::number(qRound(Units::pxToTwips(px)));
}

// One address paragraph per line, in Word's envelope style.
void addressLines(QTextCursor &cursor, const QStringList &lines, const QString &style, qreal pointSize, qreal indent,
                  qreal before, bool &first)
{
    for (int i = 0; i < lines.size(); ++i) {
        QTextBlockFormat block;
        block.setLeftMargin(indent);
        block.setTopMargin(i == 0 ? before : 0);
        block.setBottomMargin(0);
        block.setProperty(kParagraphStyleId, style);
        QTextCharFormat chars;
        chars.setFontPointSize(pointSize);
        if (first) {
            cursor.setBlockFormat(block);
            cursor.setBlockCharFormat(chars);
            first = false;
        } else {
            cursor.insertBlock(block, chars);
        }
        cursor.insertText(lines.at(i), chars);
    }
}

LabelProduct label(const char *vendor, const char *product, const char *description, bool a4, qreal top, qreal side,
                   qreal vpitch, qreal hpitch, qreal height, qreal width, int across, int down)
{
    const qreal unit = a4 ? kMm : kInch;
    LabelProduct p;
    p.vendor = QString::fromLatin1(vendor);
    p.product = QString::fromLatin1(product);
    p.description = QString::fromLatin1(description);
    p.page = a4 ? QSizeF(210 * kMm, 297 * kMm) : QSizeF(8.5 * kInch, 11 * kInch);
    p.top = top * unit;
    p.side = side * unit;
    p.verticalPitch = vpitch * unit;
    p.horizontalPitch = hpitch * unit;
    p.height = height * unit;
    p.width = width * unit;
    p.across = across;
    p.down = down;
    return p;
}

} // namespace

QList<EnvelopeSize> envelopeSizes()
{
    auto in = [](qreal w, qreal h) { return QSizeF(w * kInch, h * kInch); };
    auto mm = [](qreal w, qreal h) { return QSizeF(w * kMm, h * kMm); };
    return {{QStringLiteral("10"), QStringLiteral("Size 10 (4 1/8 x 9 1/2 in)"), in(9.5, 4.125), 20},
            {QStringLiteral("9"), QStringLiteral("Size 9 (3 7/8 x 8 7/8 in)"), in(8.875, 3.875), 19},
            {QStringLiteral("11"), QStringLiteral("Size 11 (4 1/2 x 10 3/8 in)"), in(10.375, 4.5), 21},
            {QStringLiteral("12"), QStringLiteral("Size 12 (4 3/4 x 11 in)"), in(11, 4.75), 22},
            {QStringLiteral("14"), QStringLiteral("Size 14 (5 x 11 1/2 in)"), in(11.5, 5), 23},
            {QStringLiteral("Monarch"), QStringLiteral("Monarch (3 7/8 x 7 1/2 in)"), in(7.5, 3.875), 37},
            {QStringLiteral("6.75"), QStringLiteral("Size 6 3/4 (3 5/8 x 6 1/2 in)"), in(6.5, 3.625), 38},
            {QStringLiteral("DL"), QStringLiteral("DL (110 x 220 mm)"), mm(220, 110), 27},
            {QStringLiteral("C4"), QStringLiteral("C4 (229 x 324 mm)"), mm(324, 229), 30},
            {QStringLiteral("C5"), QStringLiteral("C5 (162 x 229 mm)"), mm(229, 162), 28},
            {QStringLiteral("C6"), QStringLiteral("C6 (114 x 162 mm)"), mm(162, 114), 31},
            {QStringLiteral("B5"), QStringLiteral("B5 (176 x 250 mm)"), mm(250, 176), 34}};
}

QPointF autoDeliveryPosition(const QSizeF &size)
{
    // Word's Envelope Address frame: centred across the envelope, the text
    // two inches into it, the block in the lower half.
    return {size.width() * 0.42, size.height() * 0.5};
}

QPointF autoReturnPosition(const QSizeF &size)
{
    Q_UNUSED(size);
    return {kEnvelopeMargin, kEnvelopeMargin};
}

PageLayout envelopeLayout(const EnvelopeSpec &spec)
{
    PageLayout layout;
    layout.pageSize = spec.size.width() >= spec.size.height() ? spec.size : spec.size.transposed();
    const QPointF from = spec.returnAt.x() >= 0 ? spec.returnAt : autoReturnPosition(layout.pageSize);
    layout.margins = QMarginsF(qMax<qreal>(0, from.x()), qMax<qreal>(0, from.y()), kEnvelopeMargin, kEnvelopeMargin);
    layout.headerDistance = kEnvelopeMargin;
    layout.footerDistance = kEnvelopeMargin;
    layout.gutter = 0;
    return layout;
}

QString envelopeSectionXml(const EnvelopeSpec &spec)
{
    const PageLayout l = envelopeLayout(spec);
    return QStringLiteral(
               "<w:sectPr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
               "<w:pgSz w:w=\"%1\" w:h=\"%2\" w:orient=\"landscape\"%3/>"
               "<w:pgMar w:top=\"%4\" w:right=\"%5\" w:bottom=\"%6\" w:left=\"%7\" w:header=\"%8\" w:footer=\"%8\" w:gutter=\"0\"/>"
               "<w:cols w:space=\"720\"/></w:sectPr>")
        .arg(twipsText(l.pageSize.width()), twipsText(l.pageSize.height()),
             spec.paperCode > 0 ? QStringLiteral(" w:code=\"%1\"").arg(spec.paperCode) : QString(),
             twipsText(l.margins.top()), twipsText(l.margins.right()), twipsText(l.margins.bottom()),
             twipsText(l.margins.left()), twipsText(l.headerDistance));
}

void insertEnvelope(QTextCursor &cursor, const EnvelopeSpec &spec, const QString &sectionXml,
                    const std::function<void(QTextCursor &)> &deliveryFill)
{
    const PageLayout layout = envelopeLayout(spec);
    const QPointF delivery = spec.deliveryAt.x() >= 0 ? spec.deliveryAt : autoDeliveryPosition(layout.pageSize);
    QFont font = cursor.document()->defaultFont();
    font.setPointSizeF(10);
    const qreal line = QFontMetricsF(font).lineSpacing();
    const QStringList returnLines = spec.omitReturn || spec.returnAddress.trimmed().isEmpty()
        ? QStringList()
        : spec.returnAddress.split(QLatin1Char('\n'));
    bool first = true; // AGENT-CONTRACT: the caller hands an empty block
    addressLines(cursor, returnLines, QStringLiteral("EnvelopeReturn"), 10, 0, 0, first);
    const qreal used = layout.margins.top() + returnLines.size() * line;
    const qreal indent = qMax<qreal>(0, delivery.x() - layout.margins.left());
    const qreal before = qMax<qreal>(0, delivery.y() - used);
    if (deliveryFill) {
        addressLines(cursor, {QString()}, QStringLiteral("EnvelopeAddress"), 12, indent, before, first);
        deliveryFill(cursor);
    } else {
        addressLines(cursor, spec.delivery.split(QLatin1Char('\n')), QStringLiteral("EnvelopeAddress"), 12, indent,
                     before, first);
    }
    if (!sectionXml.isEmpty()) {
        // The envelope is a section of its own (M88, docx_sections.h).
        QTextBlockFormat end = cursor.blockFormat();
        markSectionEnd(end, newSectionKey(), sectionXml);
        end.setPageBreakPolicy(end.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysAfter);
        cursor.setBlockFormat(end);
    }
}

QList<LabelProduct> labelProducts()
{
    // Avery's sheets as Word's Label Options lists them: top margin, side
    // margin, vertical and horizontal pitch, label height and width (inches
    // for US Letter, millimetres for A4), labels across and down.
    return {label("Avery US Letter", "5160", "Address Labels", false, 0.5, 0.1875, 1, 2.75, 1, 2.625, 3, 10),
            label("Avery US Letter", "5161", "Address Labels", false, 0.5, 0.15625, 1, 4.1875, 1, 4, 2, 10),
            label("Avery US Letter", "5162", "Address Labels", false, 0.8333, 0.15625, 1.3333, 4.1875, 1.3333, 4, 2, 7),
            label("Avery US Letter", "5163", "Shipping Labels", false, 0.5, 0.15625, 2, 4.1875, 2, 4, 2, 5),
            label("Avery US Letter", "5164", "Shipping Labels", false, 0.5, 0.15625, 3.3333, 4.1875, 3.3333, 4, 2, 3),
            label("Avery US Letter", "5165", "Full-Sheet Labels", false, 0, 0, 11, 8.5, 11, 8.5, 1, 1),
            label("Avery US Letter", "5167", "Return Address Labels", false, 0.5, 0.28125, 0.5, 2.0625, 0.5, 1.75, 4, 20),
            label("Avery US Letter", "5168", "Shipping Labels", false, 0.5, 0.5, 5, 4, 5, 3.5, 2, 2),
            label("Avery US Letter", "5195", "Return Address Labels", false, 0.5, 0.28125, 0.6667, 2.0625, 0.6667, 1.75, 4, 15),
            label("Avery US Letter", "5366", "File Folder Labels", false, 0.5, 0.53125, 0.6667, 4, 0.6667, 3.4375, 2, 15),
            label("Avery US Letter", "5371", "Business Cards", false, 0.5, 0.75, 2, 3.5, 2, 3.5, 2, 5),
            label("Avery US Letter", "8160", "Address Labels (inkjet)", false, 0.5, 0.1875, 1, 2.75, 1, 2.625, 3, 10),
            label("Avery US Letter", "8163", "Shipping Labels (inkjet)", false, 0.5, 0.15625, 2, 4.1875, 2, 4, 2, 5),
            label("Avery A4/A5", "L7159", "Address Labels", true, 12.9, 7.25, 33.9, 66.04, 33.9, 63.5, 3, 8),
            label("Avery A4/A5", "L7160", "Address Labels", true, 15.15, 7.25, 38.1, 66.04, 38.1, 63.5, 3, 7),
            label("Avery A4/A5", "L7161", "Address Labels", true, 8.7, 7.25, 46.6, 66.04, 46.6, 63.5, 3, 6),
            label("Avery A4/A5", "L7162", "Address Labels", true, 12.9, 4.65, 33.9, 101.6, 33.9, 99.1, 2, 8),
            label("Avery A4/A5", "L7163", "Address Labels", true, 15.15, 4.65, 38.1, 101.6, 38.1, 99.1, 2, 7),
            label("Avery A4/A5", "L7165", "Parcel Labels", true, 13.1, 4.65, 67.7, 101.6, 67.7, 99.1, 2, 4),
            label("Avery A4/A5", "L7166", "Parcel Labels", true, 8.85, 4.65, 93.1, 101.6, 93.1, 99.1, 2, 3),
            label("Avery A4/A5", "L7167", "Shipping Labels", true, 3.95, 5.2, 289.1, 199.6, 289.1, 199.6, 1, 1),
            label("Avery A4/A5", "L7168", "Shipping Labels", true, 5, 5.2, 143.5, 199.6, 143.5, 199.6, 1, 2),
            label("Avery A4/A5", "L7169", "Parcel Labels", true, 9.5, 4.65, 139, 101.6, 139, 99.1, 2, 2),
            label("Avery A4/A5", "L7173", "Shipping Labels", true, 6, 4.65, 57, 101.6, 57, 99.1, 2, 5),
            label("Avery A4/A5", "L7651", "Mini Labels", true, 10.7, 4.75, 21.2, 40.64, 21.2, 38.1, 5, 13),
            label("Avery A4/A5", "J8160", "Address Labels (inkjet)", true, 15.15, 7.25, 38.1, 66.04, 38.1, 63.5, 3, 7)};
}

LabelProduct labelProduct(const QString &id)
{
    for (const LabelProduct &p : labelProducts()) {
        if (p.id() == id || p.product == id) {
            return p;
        }
    }
    return labelProducts().first();
}

PageLayout labelLayout(const LabelProduct &product)
{
    PageLayout layout;
    layout.pageSize = product.page;
    const qreal width = product.side + (product.across - 1) * product.horizontalPitch + product.width;
    // AGENT-NOTE: no bottom margin, as Word sets for labels: the sheet
    // reaches the paper's last label and the paragraph Word keeps after a
    // table fits below it instead of starting a blank page.
    layout.margins = QMarginsF(product.side, product.top, qMax<qreal>(0, product.page.width() - width), 0);
    layout.headerDistance = 0;
    layout.footerDistance = 0;
    return layout;
}

int insertLabelSheet(QTextCursor &cursor, const LabelProduct &product,
                     const std::function<void(QTextCursor &, int)> &fill)
{
    const bool gap = product.hasGap() && product.across > 1;
    const int columns = product.across + (gap ? product.across - 1 : 0);
    const qreal content = labelLayout(product).contentWidth();
    QTextTableFormat format = newWordTableFormat(columns, content);
    WordBorders none;
    for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
        none.insert(QString::fromLatin1(side), WordBorder::none());
    }
    format.setProperty(kTableBorders, bordersSpec(none));
    QTextTable *table = cursor.insertTable(product.down, columns, format);
    if (table == nullptr) {
        return -1;
    }
    initWordTable(table);
    TableProperties properties = tableProperties(table);
    properties.layout = QStringLiteral("fixed");
    properties.margins = {0, 15, 0, 15};
    setTableProperties(table, properties, content);
    for (int c = 0; c < columns; ++c) {
        const bool spacer = gap && c % 2 == 1;
        setColumnWidth(table, c, qRound(Units::pxToTwips(spacer ? product.horizontalPitch - product.width : product.width)),
                       content);
    }
    applyCellVerticalAlignment(table, wholeTable(table), QTextCharFormat::AlignMiddle);
    int index = 0;
    for (int r = 0; r < product.down; ++r) {
        for (int c = 0; c < columns; ++c) {
            if (gap && c % 2 == 1) {
                continue;
            }
            QTextCursor cell = table->cellAt(r, c).firstCursorPosition();
            QTextBlockFormat block = cell.blockFormat();
            block.setLeftMargin(kLabelIndent);
            block.setRightMargin(kLabelIndent);
            block.setTopMargin(0);
            block.setBottomMargin(0);
            cell.setBlockFormat(block);
            if (fill) {
                fill(cell, index);
            }
            ++index;
        }
    }
    // The heights last, so each row is padded for what its labels hold.
    for (int r = 0; r < product.down; ++r) {
        setRowProperties(table, r, {qRound(Units::pxToTwips(product.verticalPitch)), true, true});
    }
    // The block Qt keeps before a leading table has no paragraph behind it,
    // and the one after the sheet is made tiny so it does not spill onto a
    // page of its own (Word's labels do the same).
    if (const QTextBlock before = table->firstCursorPosition().block().previous(); before.isValid() && before.text().isEmpty()) {
        QTextCursor lead(before);
        QTextBlockFormat spacer;
        spacer.setLineHeight(0.01, QTextBlockFormat::FixedHeight);
        spacer.setProperty(kParagraphSynthetic, true);
        lead.setBlockFormat(spacer);
    }
    QTextCursor after = table->lastCursorPosition();
    after.movePosition(QTextCursor::NextBlock);
    QTextBlockFormat tail = after.blockFormat();
    tail.setTopMargin(0);
    tail.setBottomMargin(0);
    after.setBlockFormat(tail);
    QTextCharFormat tiny;
    tiny.setFontPointSize(1);
    after.setBlockCharFormat(tiny);
    return table->firstPosition();
}

} // namespace QindaOffice::Docx
