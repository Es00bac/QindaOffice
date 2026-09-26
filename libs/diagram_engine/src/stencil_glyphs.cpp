// SPDX-License-Identifier: GPL-3.0-or-later
// Glyphs stencils draw over their filled outline, in local space: the
// network icons (drawn in a 24-unit grid with round 2-unit strokes, the
// Lucide convention the suite's icons follow), container header bands, the
// org card's photo, UML's actor, and flowchart details.
#include "stencils.h"

#include <QCache>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

bool is(const Shape &s, const char *id)
{
    return s.type == QLatin1String(id);
}

QPainterPath rrect(double x, double y, double w, double h, double r)
{
    QPainterPath p;
    p.addRoundedRect(QRectF(x, y, w, h), r, r);
    return p;
}

QPainterPath line(double x1, double y1, double x2, double y2)
{
    QPainterPath p;
    p.moveTo(x1, y1);
    p.lineTo(x2, y2);
    return p;
}

QPainterPath dot(double x, double y)
{
    return line(x, y, x + 0.01, y);
}

QPainterPath circle(double cx, double cy, double r)
{
    QPainterPath p;
    p.addEllipse(QPointF(cx, cy), r, r);
    return p;
}

// An arc of the circle (cx, cy, r) from `start` sweeping `sweep` degrees
// (Qt's convention: counter-clockwise, 0 = 3 o'clock).
QPainterPath arc(double cx, double cy, double r, double start, double sweep)
{
    QPainterPath p;
    const QRectF box(cx - r, cy - r, r * 2, r * 2);
    p.arcMoveTo(box, start);
    p.arcTo(box, start, sweep);
    return p;
}

// The network glyphs, 24 x 24 grid.
QPainterPath icon(const QString &type)
{
    QPainterPath p;
    if (type == QLatin1String("server")) {
        p.addPath(rrect(3, 2.5, 18, 8, 2));
        p.addPath(rrect(3, 13.5, 18, 8, 2));
        p.addPath(dot(7, 6.5));
        p.addPath(dot(7, 17.5));
        p.addPath(line(11, 6.5, 17, 6.5));
        p.addPath(line(11, 17.5, 17, 17.5));
    } else if (type == QLatin1String("desktop")) {
        p.addPath(rrect(2, 3, 20, 14, 2));
        p.addPath(line(8, 21, 16, 21));
        p.addPath(line(12, 17, 12, 21));
    } else if (type == QLatin1String("laptop")) {
        p.addPath(rrect(4, 4, 16, 12, 2));
        QPainterPath base;
        base.moveTo(4, 16);
        base.lineTo(20, 16);
        base.lineTo(22, 20);
        base.lineTo(2, 20);
        base.closeSubpath();
        p.addPath(base);
    } else if (type == QLatin1String("router")) {
        p.addPath(rrect(2, 14, 20, 8, 2));
        p.addPath(dot(6, 18));
        p.addPath(dot(10, 18));
        p.addPath(line(15, 10, 15, 14));
        p.addPath(arc(15, 10, 3.2, 40, 100));
        p.addPath(arc(15, 10, 6.4, 40, 100));
    } else if (type == QLatin1String("switch")) {
        p.addPath(rrect(2, 5, 20, 14, 2));
        p.addPath(line(6, 10, 18, 10));
        p.addPath(line(15.5, 7.5, 18, 10));
        p.addPath(line(15.5, 12.5, 18, 10));
        p.addPath(line(18, 14, 6, 14));
        p.addPath(line(8.5, 11.5, 6, 14));
        p.addPath(line(8.5, 16.5, 6, 14));
    } else if (type == QLatin1String("firewall")) {
        p.addPath(rrect(3, 3, 18, 18, 2));
        p.addPath(line(3, 9, 21, 9));
        p.addPath(line(3, 15, 21, 15));
        p.addPath(line(12, 3, 12, 9));
        p.addPath(line(8, 9, 8, 15));
        p.addPath(line(16, 9, 16, 15));
        p.addPath(line(12, 15, 12, 21));
    } else if (type == QLatin1String("networkDatabase")) {
        QPainterPath top;
        top.addEllipse(QRectF(4, 2, 16, 6));
        p.addPath(top);
        p.addPath(line(4, 5, 4, 19));
        p.addPath(line(20, 5, 20, 19));
        QPainterPath middle;
        middle.arcMoveTo(QRectF(4, 9, 16, 6), 180);
        middle.arcTo(QRectF(4, 9, 16, 6), 180, 180);
        p.addPath(middle);
        QPainterPath bottom;
        bottom.arcMoveTo(QRectF(4, 16, 16, 6), 180);
        bottom.arcTo(QRectF(4, 16, 16, 6), 180, 180);
        p.addPath(bottom);
    } else if (type == QLatin1String("printer")) {
        QPainterPath body;
        body.moveTo(6, 18);
        body.lineTo(4, 18);
        body.arcTo(QRectF(2, 14, 4, 4), 270, -90);
        body.lineTo(2, 11);
        body.arcTo(QRectF(2, 9, 4, 4), 180, -90);
        body.lineTo(20, 9);
        body.arcTo(QRectF(18, 9, 4, 4), 90, -90);
        body.lineTo(22, 16);
        body.arcTo(QRectF(18, 14, 4, 4), 0, -90);
        body.lineTo(18, 18);
        p.addPath(body);
        QPainterPath paper;
        paper.moveTo(6, 9);
        paper.lineTo(6, 3);
        paper.lineTo(18, 3);
        paper.lineTo(18, 9);
        p.addPath(paper);
        p.addPath(rrect(6, 14, 12, 8, 1));
    } else if (type == QLatin1String("phone")) {
        p.addPath(rrect(6, 2, 12, 20, 2));
        p.addPath(dot(12, 18));
    }
    return p;
}

QPen glyphPen(const QColor &ink, double width)
{
    QPen pen(ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(false);
    return pen;
}

// Draws `glyph` (24 x 24 grid) centred in `box`, 2-unit strokes scaled.
void paintIcon(QPainter *painter, const QPainterPath &glyph, const QRectF &box, const QColor &ink)
{
    const double side = std::min(box.width(), box.height());
    const double scale = side / 24.0;
    painter->save();
    painter->translate(box.center().x() - side / 2, box.center().y() - side / 2);
    painter->scale(scale, scale);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(glyphPen(ink, 2.0));
    painter->drawPath(glyph);
    painter->restore();
}

QPainterPath person()
{
    QPainterPath p = circle(12, 8, 4);
    QPainterPath shoulders;
    shoulders.moveTo(4, 21);
    shoulders.lineTo(4, 19);
    shoulders.cubicTo(4, 16.8, 5.8, 15, 8, 15);
    shoulders.lineTo(16, 15);
    shoulders.cubicTo(18.2, 15, 20, 16.8, 20, 19);
    shoulders.lineTo(20, 21);
    p.addPath(shoulders);
    return p;
}

// Decoded picture bytes, cached by content so repaints never re-decode.
QImage decoded(const QByteArray &bytes)
{
    static QCache<size_t, QImage> cache(24);
    const size_t key = qHash(bytes);
    if (QImage *hit = cache.object(key)) {
        return *hit;
    }
    QImage image = QImage::fromData(bytes);
    cache.insert(key, new QImage(image));
    return image;
}

void paintCard(QPainter *painter, const Shape &s, const QColor &ink)
{
    const double h = s.rect.height();
    const double photo = std::min(h - 16, 44.0);
    const QRectF circleBox(12, (h - photo) / 2, photo, photo);
    painter->save();
    // Accent strip down the card's left edge, clipped to the card.
    // AGENT-GUARD: clip with IntersectClip inside save/restore, never
    // setClipping(false) — print and export paint under their own clip.
    painter->save();
    painter->setClipPath(outline(s), Qt::IntersectClip);
    painter->fillRect(QRectF(0, 0, 5, h), ink);
    painter->restore();
    QPainterPath disc;
    disc.addEllipse(circleBox);
    const QImage image = s.image.isEmpty() ? QImage() : decoded(s.image);
    if (!image.isNull()) {
        painter->save();
        painter->setClipPath(disc, Qt::IntersectClip);
        const QSizeF fitted = image.size().toSizeF().scaled(circleBox.size(), Qt::KeepAspectRatioByExpanding);
        painter->drawImage(QRectF(circleBox.center() - QPointF(fitted.width() / 2, fitted.height() / 2), fitted), image);
        painter->restore();
    } else {
        QColor wash = ink;
        wash.setAlphaF(0.14);
        painter->fillPath(disc, wash);
        paintIcon(painter, person(), circleBox.adjusted(photo * 0.2, photo * 0.2, -photo * 0.2, -photo * 0.2), ink);
    }
    painter->restore();
}

void paintActor(QPainter *painter, const Shape &s, const QColor &ink)
{
    const double w = s.rect.width();
    const double h = s.rect.height();
    const double head = std::min(w, h) * 0.36;
    QPainterPath p = circle(w / 2, head / 2 + 1, head / 2);
    p.addPath(line(w / 2, head + 1, w / 2, h * 0.62));
    p.addPath(line(w * 0.08, h * 0.36, w * 0.92, h * 0.36));
    p.addPath(line(w / 2, h * 0.62, w * 0.12, h - 1));
    p.addPath(line(w / 2, h * 0.62, w * 0.88, h - 1));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(glyphPen(ink, std::max(1.0, s.style.lineWidth)));
    painter->drawPath(p);
}

} // namespace

void paintGlyph(QPainter *painter, const Shape &s, const QColor &ink)
{
    const double w = s.rect.width();
    const double h = s.rect.height();
    painter->save();
    const QPainterPath glyph = icon(s.type);
    if (!glyph.isEmpty()) {
        const double inset = std::min(w, h) * 0.2;
        paintIcon(painter, glyph, QRectF(inset, inset, w - inset * 2, h - inset * 2), ink);
    } else if (is(s, "orgPerson") || is(s, "orgExecutive") || is(s, "orgVacancy")) {
        paintCard(painter, s, ink);
    } else if (is(s, "umlActor")) {
        paintActor(painter, s, ink);
    } else if (is(s, "picture")) {
        const QImage image = s.image.isEmpty() ? QImage() : decoded(s.image);
        if (!image.isNull()) {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawImage(QRectF(0, 0, w, h), image);
        } else {
            painter->setPen(glyphPen(ink, 1));
            painter->drawRect(QRectF(0, 0, w, h));
        }
    } else if (is(s, "predefinedProcess")) {
        const double inset = std::min(w * 0.1, 14.0);
        painter->setPen(glyphPen(ink, s.style.lineWidth));
        painter->drawLine(QPointF(inset, 0), QPointF(inset, h));
        painter->drawLine(QPointF(w - inset, 0), QPointF(w - inset, h));
    } else if (is(s, "database")) {
        const double e = std::min(h * 0.28, w * 0.35);
        painter->setPen(glyphPen(ink, s.style.lineWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(QRectF(0, 0, w, e), 180 * 16, 180 * 16);
    } else if (is(s, "note")) {
        const double fold = std::min(w, h) * 0.2;
        QPainterPath flap;
        flap.moveTo(w - fold, 0);
        flap.lineTo(w - fold, fold);
        flap.lineTo(w, fold);
        QColor shade = ink;
        shade.setAlphaF(0.18);
        painter->setPen(glyphPen(ink, s.style.lineWidth));
        painter->setBrush(shade);
        painter->drawPath(flap);
    } else if (is(s, "umlPackage")) {
        painter->setPen(glyphPen(ink, s.style.lineWidth));
        painter->drawLine(QPointF(0, h * 0.18), QPointF(w * 0.42, h * 0.18));
    } else {
        const QRectF header = headerRect(s);
        if (!header.isEmpty()) {
            QColor band = ink;
            band.setAlphaF(is(s, "phase") ? 0.10 : 0.16);
            painter->save();
            painter->setClipPath(outline(s), Qt::IntersectClip);
            painter->fillRect(header, band);
            painter->restore();
            painter->setPen(glyphPen(ink, std::max(0.75, s.style.lineWidth * 0.75)));
            if (is(s, "laneHorizontal")) {
                painter->drawLine(header.topRight(), header.bottomRight());
            } else {
                painter->drawLine(header.bottomLeft(), header.bottomRight());
            }
        }
    }
    painter->restore();
}

} // namespace QindaOffice::Diagram
