// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's own marks on the slide (M75, D-084; slide_canvas_item.h):
// dashed placeholder frames in the Slide Master view, a speech bubble per
// comment thread at what it is anchored to, and a play mark on audio and
// video; M82 (D-091): an empty placeholder's frame and PowerPoint's prompt.
// The renderer never draws these: the show, the thumbnails, print and PDF
// show the slide as it is.

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

// PowerPoint's prompt in an empty placeholder of `type` ("" = none: the
// date, footer and slide number show nothing until they hold something).
QString promptFor(const QString &type)
{
    if (type == u"title" || type == u"ctrTitle") {
        return SlideCanvasItem::tr("Click to add title");
    }
    if (type == u"subTitle") {
        return SlideCanvasItem::tr("Click to add subtitle");
    }
    if (type == u"pic") {
        return SlideCanvasItem::tr("Picture placeholder");
    }
    if (type == u"dt" || type == u"ftr" || type == u"sldNum" || type == u"hdr") {
        return {};
    }
    return SlideCanvasItem::tr("Click to add text");
}

bool emptyText(const ShowModel::Shape &shape)
{
    for (const ShowModel::TextParagraph &paragraph : shape.paragraphs) {
        for (const ShowModel::TextRun &run : paragraph.runs) {
            if (!run.text.isEmpty()) {
                return false;
            }
        }
    }
    return true;
}

void paintPrompt(QPainter *painter, const ShowModel::Shape &shape, const ShowModel::TextRun *look)
{
    QString prompt = promptFor(shape.placeholder.type);
    // A bulleted body's prompt wears its bullet, as PowerPoint's does.
    if (!prompt.isEmpty() && !shape.paragraphs.isEmpty() && shape.paragraphs.first().bullet
        && shape.paragraphs.first().autoNumber.isEmpty()) {
        const QString mark = shape.paragraphs.first().bulletChar;
        prompt = (mark.isEmpty() ? QStringLiteral("•") : mark) + QLatin1Char(' ') + prompt;
    }
    const QRectF rect = shape.rect.normalized();
    QPen frame(kFrame, 0);
    frame.setStyle(Qt::DashLine);
    painter->setPen(frame);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect);
    if (prompt.isEmpty()) {
        return;
    }
    QFont font = painter->font();
    if (look != nullptr && !look->fontFamily.isEmpty()) {
        font.setFamily(look->fontFamily);
    }
    // Slide points are the canvas's units: the size goes in as pixels.
    font.setPixelSize(qMax(4, qRound(look != nullptr ? look->pointSize : 18.0)));
    font.setBold(look != nullptr && look->bold);
    painter->setFont(font);
    QColor ink = look != nullptr && look->color.isValid() ? look->color : QColor(Qt::black);
    ink.setAlphaF(0.55);
    painter->setPen(ink);
    const ShowModel::TextBodyStyle &body = shape.textBody;
    const QRectF inner = rect.adjusted(body.insetLeft, body.insetTop, -body.insetRight, -body.insetBottom);
    const int vertical = body.anchor == ShowModel::TextAnchor::Middle ? Qt::AlignVCenter
        : body.anchor == ShowModel::TextAnchor::Bottom                ? Qt::AlignBottom
                                                                      : Qt::AlignTop;
    const int alignment = shape.paragraphs.isEmpty() ? 0 : shape.paragraphs.first().alignment & Qt::AlignHorizontal_Mask;
    painter->drawText(inner, (alignment != 0 ? alignment : Qt::AlignLeft) | vertical | Qt::TextWordWrap, prompt);
}

} // namespace

void SlideCanvasItem::setPlaceholderLooks(QHash<QString, ShowModel::TextRun> looks)
{
    m_placeholderLooks = std::move(looks);
    update();
}

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
        // M82: an empty placeholder on a slide shows where to type.
        if (!m_templateMode && shape.placeholder.isPlaceholder() && shape.id != m_textShapeId
            && shape.kind == ShowModel::ShapeKind::TextBox && emptyText(shape)) {
            const auto look = m_placeholderLooks.constFind(shape.id);
            paintPrompt(painter, shape, look != m_placeholderLooks.constEnd() ? &look.value() : nullptr);
        }
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
