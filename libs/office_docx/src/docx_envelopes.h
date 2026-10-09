// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's envelopes and labels (M77, D-086).
//
// An envelope is a section of its own: its page is the envelope (landscape,
// Word's paper code), the return address at the top left in Word's
// "Envelope Return" style, the delivery address in "Envelope Address"
// placed where Word's Auto puts it (or where the user said) by its indent
// and the space before it. Added to a document it is the first section;
// as a document of its own (Start Mail Merge ▸ Envelopes) the document's
// page layout is the envelope.
//
// A label sheet is Word's fixed-layout table: one column per label across
// with a spacer column between labels that have a gap, rows exactly the
// vertical pitch that cannot break, no borders, the page's margins the
// sheet's top and side margins. The products are Avery's US Letter and A4
// sheets with the dimensions Word's Label Options lists.

#include "docx_model.h"

#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <functional>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Docx {

struct EnvelopeSize {
    QString id;    // "10", "DL", "C5", …
    QString name;  // "Size 10 (4 1/8 x 9 1/2 in)"
    QSizeF size;   // px, landscape (width > height)
    int paperCode = 0; // w:pgSz/@w:code (Windows DMPAPER_ENV_*)
};
[[nodiscard]] QList<EnvelopeSize> envelopeSizes();

struct EnvelopeSpec {
    QString delivery;          // lines separated by '\n'
    QString returnAddress;
    bool omitReturn = false;
    QSizeF size{912, 396};     // px, landscape: #10
    int paperCode = 20;
    QPointF deliveryAt{-1, -1}; // from the page's left and top edge, px; negative: Word's Auto
    QPointF returnAt{-1, -1};
};
// Where Word's Auto puts the addresses on an envelope of `size`.
[[nodiscard]] QPointF autoDeliveryPosition(const QSizeF &size);
[[nodiscard]] QPointF autoReturnPosition(const QSizeF &size);
// The envelope's page: its size and Word's envelope margins.
[[nodiscard]] PageLayout envelopeLayout(const EnvelopeSpec &spec);
// The w:sectPr of an envelope section (page size, paper code, margins).
[[nodiscard]] QString envelopeSectionXml(const EnvelopeSpec &spec);

// Writes the envelope's paragraphs from `cursor`, which must be in an empty
// paragraph: the return address (unless omitted), then the delivery address. `deliveryFill`, when set, fills the
// delivery block instead of `spec.delivery` (a merge's «AddressBlock»).
// With a non-empty `sectionXml` the last paragraph ends the section with
// it and the next page begins (an envelope added before a letter).
void insertEnvelope(QTextCursor &cursor, const EnvelopeSpec &spec, const QString &sectionXml,
                    const std::function<void(QTextCursor &)> &deliveryFill = {});

struct LabelProduct {
    QString vendor;        // "Avery US Letter", "Avery A4/A5"
    QString product;       // "5160"
    QString description;   // "Address Labels"
    QSizeF page;           // px
    qreal top = 0;         // top margin, px
    qreal side = 0;        // left margin, px
    qreal verticalPitch = 0;
    qreal horizontalPitch = 0;
    qreal height = 0;
    qreal width = 0;
    int across = 1;
    int down = 1;

    [[nodiscard]] int count() const { return across * down; }
    [[nodiscard]] bool hasGap() const { return horizontalPitch - width > 0.5; }
    [[nodiscard]] QString id() const { return vendor + QLatin1Char('|') + product; }
};
[[nodiscard]] QList<LabelProduct> labelProducts();
[[nodiscard]] LabelProduct labelProduct(const QString &id);
// The sheet's page: its paper, the top and side margins, the rest of the
// width as the right margin, no bottom margin (Word's).
[[nodiscard]] PageLayout labelLayout(const LabelProduct &product);

// Inserts the label sheet at `cursor` (the start of an empty document):
// `fill(cell, index)` writes label `index` (0-based, left to right, top
// to bottom) into its cell; spacer cells stay empty. Returns the table's
// first position, or −1. Each row is padded to the sheet's pitch from the
// labels' laid-out height; inside an edit block Qt has not laid them out
// yet, so refresh the rows after it ends (docx_table_format.h
// refreshRowHeights).
int insertLabelSheet(QTextCursor &cursor, const LabelProduct &product,
                     const std::function<void(QTextCursor &, int)> &fill);

} // namespace QindaOffice::Docx
