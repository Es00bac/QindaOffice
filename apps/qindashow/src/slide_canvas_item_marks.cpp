// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's own marks on the slide (M75, D-084; slide_canvas_item.h):
// dashed placeholder frames in the Slide Master view, a speech bubble per
// comment thread at what it is anchored to, and a play mark on audio and
// video. The renderer never draws these: the show, the thumbnails, print
// and PDF show the slide as it is.

#include "slide_canvas_item.h"

#include <QPainter>
#include <QPainterPath>

namespace QindaOffice::Apps::Show {

namespace {

// AGENT-NOTE: the canvas's marks keep its own fixed colours (kSelection,
// kGuide in slide_canvas_item.cpp): they sit on the paper, not the chrome.
const QColor kFrame(130, 130, 130);
const QColor kComment(0xF2, 0xA9, 0x00);
const QColor kMedia(30, 30, 30, 200);

void paintBubble(QPainter *painter, const QPointF &at, int number)
{
    const QRectF box(at, QSizeF(18, 13));
    QPainterPath bubble;
    bubble.addRoundedRect(box, 2.5, 2.5);
    QPainterPath tail;
    tail.moveTo(box.left() + 4, box.bottom() - 0.5);
    tail.lineTo(box.left() + 4, box.bottom() + 4);
    tail.lineTo(box.left() + 9, box.bottom() - 0.5);
    tail.closeSubpath();
    painter->fillPath(bubble.united(tail), kComment);
    painter->setPen(QPen(Qt::white, 0));
    QFont font = painter->font();
    font.setPointSizeF(7.5);
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(box, Qt::AlignCenter, QString::number(number));
}

void paintPlayMark(QPainter *painter, const QRectF &rect)
{
    const qreal r = qBound(6.0, qMin(rect.width(), rect.height()) * 0.12, 16.0);
    const QPointF centre(rect.left() + r + 4, rect.bottom() - r - 4);
    painter->setPen(Qt::NoPen);
    painter->setBrush(kMedia);
    painter->drawEllipse(centre, r, r);
    QPainterPath play;
    play.moveTo(centre + QPointF(-r * 0.35, -r * 0.5));
    play.lineTo(centre + QPointF(r * 0.55, 0));
    play.lineTo(centre + QPointF(-r * 0.35, r * 0.5));
    play.closeSubpath();
    painter->fillPath(play, Qt::white);
}

} // namespace

void SlideCanvasItem::setTemplateMode(bool on)
{
    if (m_templateMode != on) {
        m_templateMode = on;
        update();
    }
}

void SlideCanvasItem::paintMarks(QPainter *painter) const
{
    if (m_slide == nullptr) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    for (const ShowModel::Shape &shape : m_slide->shapes) {
        if (m_templateMode && shape.placeholder.isPlaceholder()) {
            QPen frame(kFrame, 0);
            frame.setStyle(Qt::DashLine);
            painter->setPen(frame);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(shape.rect.normalized());
        }
        if (shape.media.isMedia() && shape.media.kind == ShowModel::MediaKind::Video) {
            paintPlayMark(painter, shape.rect.normalized());
        }
    }
    // One bubble per thread, at the shape it is on (its top right) or where
    // it was put on the slide; threads at one place step aside.
    QHash<QString, int> atPlace;
    int number = 0;
    for (const ShowModel::SlideComment &comment : m_slide->comments) {
        if (comment.isReply()) {
            continue;
        }
        ++number;
        QPointF at = comment.position.isNull() ? QPointF(6, 6) : comment.position;
        QString place = QStringLiteral("slide");
        for (const ShowModel::Shape &shape : m_slide->shapes) {
            if (!comment.shapeId.isEmpty() && shape.id == comment.shapeId) {
                at = shape.rect.normalized().topRight() + QPointF(2, -8);
                place = shape.id;
            }
        }
        const int stacked = atPlace.value(place);
        atPlace.insert(place, stacked + 1);
        paintBubble(painter, at + QPointF(stacked * 20, 0), number);
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Show
