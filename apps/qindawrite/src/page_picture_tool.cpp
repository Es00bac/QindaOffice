// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_picture_tool.h"

#include "docx_float.h"
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

// A default QTextImageFormat is technically valid but names no image.
// Callers test name(), so preserved/native graphics cannot enter Picture
// commands that would discard their drawing semantics (D-101).
QTextImageFormat imageAt(const QTextDocument *document, int position)
{
    if (position < 0 || position >= document->characterCount()
        || document->characterAt(position) != QChar::ObjectReplacementCharacter) {
        return {};
    }
    // The fragment's own format: a floating picture's ObjectIndex (its
    // frame) is kept, so resizing it keeps it floating (docx_float.h).
    const QTextCharFormat format = Docx::characterFormatAt(*document, position);
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
    if (image.name().isEmpty()) {
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
    if (image.name().isEmpty()) {
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
    // Inline, in a floating frame, or over the text (docx_float.h).
    return !imageAt(document(), position).name().isEmpty() ? Docx::floatingPictureRect(*document(), position) : QRectF();
}

QList<int> PictureTool::floatingPictures() const
{
    QList<int> positions;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isImageFormat() && Docx::pictureFloat(fragment.charFormat()).isFloating()) {
                for (int i = 0; i < fragment.length(); ++i) {
                    positions.append(fragment.position() + i);
                }
            }
        }
    }
    return positions;
}

int PictureTool::pictureAt(const QPointF &docPos) const
{
    // In front of the text and wrapped pictures first; one behind the text
    // only where no text is under the pointer (Word's rule).
    int behind = -1;
    for (const int position : floatingPictures()) {
        if (imageAt(document(), position).name().isEmpty() || !pictureRect(position).contains(docPos)) {
            continue;
        }
        if (Docx::pictureFloat(imageAt(document(), position)).wrap == u"behind") {
            behind = position;
            continue;
        }
        return position;
    }
    const int near = m_control->cursorForPosition(docPos).position();
    for (const int position : {near, near - 1}) {
        const QTextImageFormat image = imageAt(document(), position);
        if (!image.name().isEmpty() && !Docx::pictureFloat(image).isFloating() && pictureRect(position).contains(docPos)) {
            return position;
        }
    }
    if (behind >= 0 && document()->documentLayout()->hitTest(docPos, Qt::ExactHit) < 0) {
        return behind;
    }
    return -1;
}

bool PictureTool::selectedFloats() const
{
    const int position = selected();
    return position >= 0 && Docx::pictureFloat(imageAt(document(), position)).isFloating();
}

bool PictureTool::beginMove(const QPointF &docPos)
{
    const int position = selected();
    if (!selectedFloats() || !pictureRect(position).contains(docPos)) {
        return false;
    }
    m_position = position;
    m_moving = true;
    m_moveFrom = docPos;
    m_start = pictureRect(position);
    m_drag = m_start;
    return true;
}

bool PictureTool::movePicture(int position, const QPointF &delta)
{
    if (imageAt(document(), position).name().isEmpty() || delta.isNull()) {
        return false;
    }
    const Docx::PictureFloat moved = Docx::movedPictureFloat(*document(), position, delta);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    const int at = Docx::setPictureFloat(document(), position, moved);
    cursor.endEditBlock();
    if (at >= 0) {
        select(at);
    }
    return at >= 0;
}

int PictureTool::selected() const
{
    const QTextCursor cursor = m_control->textCursor();
    const int start = cursor.selectionStart();
    if (cursor.selectionEnd() - start != 1 || imageAt(document(), start).name().isEmpty()) {
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
    if (m_moving) {
        m_drag = m_start.translated(docPos - m_moveFrom);
        return;
    }
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
    if (m_moving) {
        m_moving = false;
        return movePicture(m_position, m_drag.topLeft() - m_start.topLeft());
    }
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
    if (image.name().isEmpty() || size.width() < 1 || size.height() < 1) {
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
    if (m_handle >= 0 || m_moving) {
        QPen ghost(kHandleEdge);
        ghost.setStyle(Qt::DashLine);
        painter->setPen(ghost);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_drag);
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
