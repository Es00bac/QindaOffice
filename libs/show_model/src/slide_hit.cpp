// SPDX-License-Identifier: GPL-3.0-or-later
// The action under the pointer in the slide show (slide_hit.h).

#include "slide_hit.h"

#include "preset_geometry.h"
#include "text_painter.h"

#include <QPainterPathStroker>
#include <QTransform>

namespace QindaOffice::Show {

namespace {

QString presetName(const Shape &shape)
{
    if (!shape.geometry.isEmpty()) {
        return shape.geometry;
    }
    return shape.kind == ShapeKind::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("rect");
}

// `point` in the shape's own frame: its rotation undone about its centre.
QPointF unrotated(const Shape &shape, const QPointF &point)
{
    if (shape.rotation == 0) {
        return point;
    }
    const QPointF centre = shape.rect.normalized().center();
    QTransform turn;
    turn.translate(centre.x(), centre.y());
    turn.rotate(-shape.rotation);
    turn.translate(-centre.x(), -centre.y());
    return turn.map(point);
}

// True when the shape (unrotated `p`) covers the point: a line within a
// few points of its stroke, a shape inside its outline (else its box).
bool covers(const Shape &shape, const QPointF &p)
{
    const QRectF rect = shape.rect.normalized();
    const bool lineKind = shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow
        || (shape.kind == ShapeKind::AutoShape && Geometry::isOpenPreset(shape.geometry));
    if (lineKind) {
        QPainterPath path;
        const bool flipH = shape.flipH != (shape.rect.width() < 0);
        const bool flipV = shape.flipV != (shape.rect.height() < 0);
        path.moveTo(flipH ? rect.right() : rect.left(), flipV ? rect.bottom() : rect.top());
        path.lineTo(flipH ? rect.left() : rect.right(), flipV ? rect.top() : rect.bottom());
        if (shape.kind == ShapeKind::AutoShape) {
            path = Geometry::presetOutline(shape.geometry, rect, shape.adjust).stroke;
        }
        QPainterPathStroker stroker;
        stroker.setWidth(qMax<qreal>(8.0, shape.strokeWidth + 6.0));
        return stroker.createStroke(path).contains(p);
    }
    if (!rect.adjusted(-1, -1, 1, 1).contains(p)) {
        return false;
    }
    if (shape.kind == ShapeKind::Ellipse || shape.kind == ShapeKind::AutoShape) {
        const Geometry::Outline outline = shape.customGeometry.isEmpty()
            ? Geometry::presetOutline(presetName(shape), rect, shape.adjust)
            : Geometry::customOutline(shape.customGeometry, rect);
        if (!outline.fill.isEmpty()) {
            return outline.fill.contains(p);
        }
    }
    return true;
}

// The action of linked text under `p` in `shape` (its text box as the
// renderer lays it out).
QString textLinkAt(const Shape &shape, const QPointF &p)
{
    if (!shape.holdsText() || shape.paragraphs.isEmpty()) {
        return {};
    }
    const QRectF rect = shape.rect.normalized();
    const QRectF box = shape.kind == ShapeKind::TextBox ? rect : Geometry::textRect(presetName(shape), rect, shape.adjust);
    return Render::hyperlinkAt(box, shape.paragraphs, shape.textBody, p);
}

std::optional<ActionHit> hitShape(const Shape &shape, const QPointF &point, bool hover)
{
    const QPointF p = unrotated(shape, point);
    if (shape.kind == ShapeKind::Group && !shape.children.empty()) {
        const QRectF rect = shape.rect.normalized();
        if (!rect.adjusted(-1, -1, 1, 1).contains(p)) {
            return std::nullopt;
        }
        // Into the members' coordinate space (a:chOff / a:chExt).
        const QRectF space = shape.childRect.isValid() && !shape.childRect.isEmpty() ? shape.childRect : rect;
        const QPointF inner(space.left() + (p.x() - rect.left()) * space.width() / qMax<qreal>(1e-6, rect.width()),
                            space.top() + (p.y() - rect.top()) * space.height() / qMax<qreal>(1e-6, rect.height()));
        for (auto it = shape.children.crbegin(); it != shape.children.crend(); ++it) {
            if (it->hidden) {
                continue;
            }
            if (std::optional<ActionHit> hit = hitShape(*it, inner, hover)) {
                if (!hit->action.isEmpty()) {
                    // The member's box, in slide points.
                    const qreal sx = rect.width() / qMax<qreal>(1e-6, space.width());
                    const qreal sy = rect.height() / qMax<qreal>(1e-6, space.height());
                    const QRectF b = hit->bounds;
                    hit->bounds = QRectF(rect.left() + (b.left() - space.left()) * sx, rect.top() + (b.top() - space.top()) * sy,
                                         b.width() * sx, b.height() * sy);
                    return hit;
                }
                break; // a member without an action: the group's own applies
            }
        }
        const ShowAction &own = hover ? shape.hover : shape.click;
        return ActionHit{shape.id, own, rect, false};
    }
    if (!covers(shape, p)) {
        return std::nullopt;
    }
    if (!hover) {
        if (const QString link = textLinkAt(shape, p); !link.isEmpty()) {
            ShowAction action;
            action.link = link;
            return ActionHit{shape.id, action, shape.rect.normalized(), true};
        }
    }
    return ActionHit{shape.id, hover ? shape.hover : shape.click, shape.rect.normalized(), false};
}

} // namespace

bool shownAt(const Shape &shape, int buildStep)
{
    if (shape.hidden) {
        return false;
    }
    if (shape.media.isMedia()) {
        return !shape.media.hideWhenStopped; // drawn by the player while it plays
    }
    if (shape.buildStep > buildStep && !isEmphasis(shape.animation)) {
        return false;
    }
    return !(shape.exit.isSet() && shape.exit.step <= buildStep);
}

std::optional<ActionHit> actionAt(const Slide &slide, const QPointF &point, int buildStep, bool hover)
{
    // Topmost first; the first shape under the point decides (a shape
    // without an action hides the ones under it, as in PowerPoint).
    for (auto it = slide.shapes.crbegin(); it != slide.shapes.crend(); ++it) {
        if (!shownAt(*it, buildStep)) {
            continue;
        }
        if (std::optional<ActionHit> hit = hitShape(*it, point, hover)) {
            if (hit->action.isEmpty()) {
                return std::nullopt;
            }
            return hit;
        }
    }
    return std::nullopt;
}

} // namespace QindaOffice::Show
