// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_picture_tool.h"

#include "docx_properties.h"
#include "page_text_control.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

namespace QindaOffice::Apps::Write {

namespace {

// Paper colours (D-004): handles are drawn on the page like Word's.
const QColor kHandleFill(0xff, 0xff, 0xff);
const QColor kHandleEdge(0x2f, 0x6f, 0xd6);
constexpr qreal kHandle = 8;
constexpr qreal kMinimum = 8; // a picture never shrinks to nothing

QTextImageFormat imageAt(const QTextDocument *document, int position)
{
    if (position < 0 || position >= document->characterCount()) {
        return {};
    }
    QTextCursor cursor(const_cast<QTextDocument *>(document));
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (cursor.selectedText() != QString(QChar::ObjectReplacementCharacter)) {
        return {};
    }
    const QTextCharFormat format = cursor.charFormat();
    if (!format.isImageFormat() || format.hasProperty(Docx::kInlineXml)) {
        return {}; // a preserved object (chart, shape) is not a picture to size
    }
    return format.toImageFormat();
}

} // namespace

PictureTool::PictureTool(PageTextControl *control)
    : m_control(control)
{
}

QTextDocument *PictureTool::document() const
{
    return m_control->document();
}

QSizeF PictureTool::naturalSize(int position) const
{
    const QTextImageFormat image = imageAt(document(), position);
    if (!image.isValid()) {
        return {};
    }
    const QVariant resource = document()->resource(QTextDocument::ImageResource, QUrl(image.name()));
    const QImage decoded = resource.typeId() == QMetaType::QImage ? resource.value<QImage>()
                                                                  : QImage::fromData(resource.toByteArray());
    return decoded.isNull() ? QSizeF() : QSizeF(decoded.size());
}

QSizeF PictureTool::pictureSize(int position) const
{
    const QTextImageFormat image = imageAt(document(), position);
    if (!image.isValid()) {
        return {};
    }
    QSizeF size(image.width(), image.height());
    const QSizeF natural = naturalSize(position);
    if (size.width() <= 0 && size.height() <= 0) {
        size = natural;
    } else if (size.height() <= 0 && !natural.isEmpty()) {
        size.setHeight(size.width() * natural.height() / natural.width());
    } else if (size.width() <= 0 && !natural.isEmpty()) {
        size.setWidth(size.height() * natural.width() / natural.height());
    }
    return size;
}

QString PictureTool::altText(int position) const
{
    return imageAt(document(), position).stringProperty(Docx::kPictureAltText);
}

QRectF PictureTool::pictureRect(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    const QTextLayout *layout = block.isValid() ? block.layout() : nullptr;
    const QSizeF size = pictureSize(position);
    if (layout == nullptr || size.isEmpty()) {
        return {};
    }
    const int offset = position - block.position();
    const QTextLine line = layout->lineForTextPosition(offset);
    if (!line.isValid()) {
        return {};
    }
    // AGENT-NOTE: Qt sits an inline picture on the baseline (ascent = its
    // height, QTextDocumentLayout::positionInlineObject).
    const QPointF origin = document()->documentLayout()->blockBoundingRect(block).topLeft();
    const qreal x = origin.x() + line.cursorToX(offset);
    const qreal baseline = origin.y() + line.y() + line.ascent();
    return {x, baseline - size.height(), size.width(), size.height()};
}

int PictureTool::pictureAt(const QPointF &docPos) const
{
    const int near = m_control->cursorForPosition(docPos).position();
    for (const int position : {near, near - 1}) {
        if (imageAt(document(), position).isValid() && pictureRect(position).contains(docPos)) {
            return position;
        }
    }
    return -1;
}

int PictureTool::selected() const
{
    const QTextCursor cursor = m_control->textCursor();
    const int start = cursor.selectionStart();
    if (cursor.selectionEnd() - start != 1 || !imageAt(document(), start).isValid()) {
        return -1;
    }
    return start;
}

void PictureTool::select(int position)
{
    QTextCursor cursor(document());
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    m_control->setTextCursor(cursor);
}

QList<QRectF> PictureTool::handleRects(const QRectF &r) const
{
    const QList<QPointF> points{r.topLeft(),     QPointF(r.center().x(), r.top()),    r.topRight(),
                                QPointF(r.right(), r.center().y()), r.bottomRight(),
                                QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
                                QPointF(r.left(), r.center().y())};
    QList<QRectF> rects;
    for (const QPointF &p : points) {
        rects.append(QRectF(p - QPointF(kHandle / 2, kHandle / 2), QSizeF(kHandle, kHandle)));
    }
    return rects;
}

int PictureTool::handleAt(const QPointF &docPos) const
{
    const int position = selected();
    if (position < 0) {
        return -1;
    }
    const QList<QRectF> rects = handleRects(pictureRect(position));
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).adjusted(-3, -3, 3, 3).contains(docPos)) {
            return i;
        }
    }
    return -1;
}

bool PictureTool::beginDrag(int handle, const QPointF &docPos)
{
    Q_UNUSED(docPos);
    m_position = selected();
    if (m_position < 0 || handle < 0 || handle > 7) {
        return false;
    }
    m_handle = handle;
    m_start = pictureRect(m_position);
    m_drag = m_start;
    return true;
}

void PictureTool::dragTo(const QPointF &docPos, bool freeAspect)
{
    if (m_handle < 0) {
        return;
    }
    QRectF r = m_start;
    const bool left = m_handle == 0 || m_handle == 6 || m_handle == 7;
    const bool right = m_handle == 2 || m_handle == 3 || m_handle == 4;
    const bool top = m_handle == 0 || m_handle == 1 || m_handle == 2;
    const bool bottom = m_handle == 4 || m_handle == 5 || m_handle == 6;
    if (left) {
        r.setLeft(qMin(docPos.x(), r.right() - kMinimum));
    } else if (right) {
        r.setRight(qMax(docPos.x(), r.left() + kMinimum));
    }
    if (top) {
        r.setTop(qMin(docPos.y(), r.bottom() - kMinimum));
    } else if (bottom) {
        r.setBottom(qMax(docPos.y(), r.top() + kMinimum));
    }
    const bool corner = (left || right) && (top || bottom);
    if (corner && !freeAspect && m_start.height() > 0) {
        // Corner handles keep the picture's proportions (Word's default).
        const qreal ratio = m_start.width() / m_start.height();
        const qreal scale = qMax(r.width() / m_start.width(), r.height() / m_start.height());
        const QSizeF size(m_start.width() * scale, m_start.width() * scale / ratio);
        r = QRectF(left ? m_start.right() - size.width() : m_start.left(),
                   top ? m_start.bottom() - size.height() : m_start.top(), size.width(), size.height());
    }
    m_drag = r;
}

bool PictureTool::finishDrag()
{
    if (m_handle < 0) {
        return false;
    }
    m_handle = -1;
    if ((m_drag.size() - m_start.size()).isNull()) {
        return false;
    }
    return setPicture(m_position, m_drag.size(), altText(m_position));
}

bool PictureTool::setPicture(int position, const QSizeF &size, const QString &altText)
{
    QTextImageFormat image = imageAt(document(), position);
    if (!image.isValid() || size.width() < 1 || size.height() < 1) {
        return false;
    }
    image.setWidth(qRound(size.width() * 10) / 10.0);
    image.setHeight(qRound(size.height() * 10) / 10.0);
    if (altText.isEmpty()) {
        image.clearProperty(Docx::kPictureAltText);
    } else {
        image.setProperty(Docx::kPictureAltText, altText);
    }
    QTextCursor cursor(document());
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    cursor.setCharFormat(image); // one undo step
    select(position);
    return true;
}

void PictureTool::draw(QPainter *painter, const QRectF &clip) const
{
    const int position = selected();
    if (position < 0) {
        return;
    }
    const QRectF picture = pictureRect(position);
    if (!picture.adjusted(-kHandle, -kHandle, kHandle, kHandle).intersects(clip)) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    QPen edge(kHandleEdge);
    edge.setWidthF(1);
    painter->setPen(edge);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(picture);
    painter->setBrush(kHandleFill);
    for (const QRectF &handle : handleRects(picture)) {
        painter->drawRect(handle);
    }
    if (m_handle >= 0) {
        QPen ghost(kHandleEdge);
        ghost.setStyle(Qt::DashLine);
        painter->setPen(ghost);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_drag);
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
