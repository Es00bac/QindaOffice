// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_endnotes.h"

#include "write_footnotes.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

#include <cmath>

namespace QindaOffice::Apps::Write {

namespace {

constexpr qreal kGap = 16;           // body end → separator → first note
constexpr qreal kSeparator = 192;    // Word's rule: two inches
constexpr qreal kNotePoints = 10;    // Word's Endnote Text

} // namespace

std::shared_ptr<EndnoteArea> layoutEndnotes(const QTextDocument &body, const QHash<QString, QString> &texts,
                                            const Docx::NoteOptions &options)
{
    const QList<EndnoteRef> refs = collectEndnotes(&body, options);
    const QSizeF page = body.pageSize();
    if (refs.isEmpty() || page.height() <= 0) {
        return nullptr;
    }
    const qreal height = page.height();
    const QTextFrameFormat root = body.rootFrame()->frameFormat();
    const QRectF last = body.documentLayout()->blockBoundingRect(body.lastBlock());
    int firstPage = qMax(0, int(last.bottom() / height));
    qreal inPage = last.bottom() - firstPage * height;
    QFont font = body.defaultFont();
    font.setPointSizeF(kNotePoints);
    const qreal line = QFontMetricsF(font).lineSpacing();
    if (inPage + kGap + 2 * line > height - root.bottomMargin()) {
        ++firstPage; // no room for the rule and a note: Word starts a page
        inPage = root.topMargin();
    }
    auto area = std::make_shared<EndnoteArea>();
    area->firstPage = firstPage;
    area->notes = std::make_shared<QTextDocument>();
    QTextDocument *notes = area->notes.get();
    notes->setUndoRedoEnabled(false);
    notes->setUseDesignMetrics(true);
    notes->setDefaultFont(body.defaultFont());
    notes->setPageSize(page);
    QTextFrameFormat frame = notes->rootFrame()->frameFormat();
    frame.setLeftMargin(root.leftMargin());
    frame.setRightMargin(root.rightMargin());
    frame.setTopMargin(root.topMargin());
    frame.setBottomMargin(root.bottomMargin());
    notes->rootFrame()->setFrameFormat(frame);
    // AGENT-NOTE: Qt drops a frame's first block's top margin, so the push
    // down to the body's end is an empty first line of exactly that height.
    QTextCursor cursor(notes);
    QTextBlockFormat lead;
    lead.setLineHeight(qMax<qreal>(0.01, inPage - root.topMargin() + kGap), QTextBlockFormat::FixedHeight);
    cursor.setBlockFormat(lead);
    area->separatorY = inPage + kGap / 2;
    QTextCharFormat text;
    text.setFont(font);
    QTextCharFormat mark = text;
    mark.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    QTextBlockFormat note;
    note.setBottomMargin(2);
    for (const EndnoteRef &ref : refs) {
        cursor.insertBlock(note, text);
        cursor.insertText(ref.label, mark);
        cursor.insertText(QStringLiteral(" ") + texts.value(ref.id), text);
    }
    notes->documentLayout()->documentSize(); // lays the notes out
    area->pages = qMax(1, int(std::ceil(notes->documentLayout()->documentSize().height() / height)));
    return area;
}

void drawEndnotes(QPainter *painter, const EndnoteArea &area, int pageIndex, const QRectF &pageRect)
{
    if (area.isEmpty() || pageIndex < area.firstPage || pageIndex >= area.firstPage + area.pages) {
        return;
    }
    const int k = pageIndex - area.firstPage;
    const QSizeF page = area.notes->pageSize();
    painter->save();
    painter->translate(pageRect.topLeft());
    if (k == 0) {
        const qreal left = area.notes->rootFrame()->frameFormat().leftMargin();
        painter->setPen(QPen(Qt::black, 0.75));
        painter->drawLine(QPointF(left, area.separatorY), QPointF(left + kSeparator, area.separatorY));
    }
    painter->translate(0, -k * page.height());
    QAbstractTextDocumentLayout::PaintContext context;
    // Paper is white with black ink on every theme (D-004).
    context.palette.setColor(QPalette::Text, Qt::black);
    context.clip = QRectF(0, k * page.height(), page.width(), page.height());
    area.notes->documentLayout()->draw(painter, context);
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
