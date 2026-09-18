// SPDX-License-Identifier: GPL-3.0-or-later
// Snapping, guides and grouping for the slide canvas item.
//
// Snapping is what makes a slide look deliberate: edges and centres line up
// with the slide and with each other instead of nearly lining up. The rule
// is deliberately simple and predictable — within kSnapPt of a candidate,
// take the candidate exactly, and draw the guide that says why.
//
// Grouping is a shape field, not a scene construct: shapes sharing a
// groupId select and move as one, and the group survives save/reload
// because it travels in the model (fodp qq:group).

#include "slide_canvas_item.h"

#include <QUuid>

namespace QindaOffice::Apps::Show {

namespace {

// Slide points, not pixels: the threshold must mean the same thing at
// every zoom, or snapping gets stickier the further you zoom in.
constexpr qreal kSnapPt = 6.0;

// Replaces `value` with the nearest candidate within kSnapPt, recording
// the guide line to draw. Returns the delta applied.
qreal snapTo(qreal value, const QList<qreal> &candidates, qreal *guideAt)
{
    qreal best = 0;
    bool found = false;
    for (const qreal candidate : candidates) {
        const qreal delta = candidate - value;
        if (qAbs(delta) <= kSnapPt && (!found || qAbs(delta) < qAbs(best))) {
            best = delta;
            *guideAt = candidate;
            found = true;
        }
    }
    return found ? best : 0.0;
}

} // namespace

void SlideCanvasItem::setSnapEnabled(bool enabled)
{
    if (m_snap == enabled) {
        return;
    }
    m_snap = enabled;
    if (!enabled) {
        clearGuides();
    }
    emit snapChanged();
}

void SlideCanvasItem::clearGuides()
{
    if (m_guides.isEmpty()) {
        return;
    }
    m_guides.clear();
    update();
}

QRectF SlideCanvasItem::snapRect(const QString &shapeId, const QRectF &proposed)
{
    m_guides.clear();
    if (!m_snap || m_slide == nullptr) {
        return proposed;
    }
    const QRectF rect = proposed.normalized();
    // Candidates: the slide's edges and centre, plus every other shape's
    // edges and centre — the lines a person would line up against.
    QList<qreal> xs{0.0, m_slideSize.width() / 2.0, m_slideSize.width()};
    QList<qreal> ys{0.0, m_slideSize.height() / 2.0, m_slideSize.height()};
    for (const ShowModel::Shape &other : m_slide->shapes) {
        if (other.id == shapeId || m_gestureStart.contains(other.id)) {
            continue; // a shape moving with the drag is not a target
        }
        const QRectF bounds = other.rect.normalized();
        xs << bounds.left() << bounds.center().x() << bounds.right();
        ys << bounds.top() << bounds.center().y() << bounds.bottom();
    }
    qreal guideX = 0;
    qreal guideY = 0;
    bool haveX = false;
    bool haveY = false;
    // Left, centre and right all compete; the smallest movement wins, which
    // is what stops a wide shape jumping a whole half-slide.
    qreal dx = 0;
    for (const qreal edge : {rect.left(), rect.center().x(), rect.right()}) {
        qreal at = 0;
        const qreal delta = snapTo(edge, xs, &at);
        if (delta != 0.0 && (!haveX || qAbs(delta) < qAbs(dx))) {
            dx = delta;
            guideX = at;
            haveX = true;
        }
    }
    qreal dy = 0;
    for (const qreal edge : {rect.top(), rect.center().y(), rect.bottom()}) {
        qreal at = 0;
        const qreal delta = snapTo(edge, ys, &at);
        if (delta != 0.0 && (!haveY || qAbs(delta) < qAbs(dy))) {
            dy = delta;
            guideY = at;
            haveY = true;
        }
    }
    if (haveX) {
        m_guides.append(QLineF(guideX, 0, guideX, m_slideSize.height()));
    }
    if (haveY) {
        m_guides.append(QLineF(0, guideY, m_slideSize.width(), guideY));
    }
    return proposed.translated(dx, dy);
}

bool SlideCanvasItem::selectionIsGrouped() const
{
    if (m_slide == nullptr) {
        return false;
    }
    for (const QString &id : m_selected) {
        const ShowModel::Shape *shape = m_slide->findShape(id);
        if (shape != nullptr && !shape->groupId.isEmpty()) {
            return true;
        }
    }
    return false;
}

void SlideCanvasItem::groupSelected()
{
    if (m_slide == nullptr || m_selected.size() < 2) {
        return;
    }
    emit compoundEditStarted();
    const QString group = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    for (ShowModel::Shape &shape : m_slide->shapes) {
        if (m_selected.contains(shape.id)) {
            shape.groupId = group;
        }
    }
    emit slideEdited();
    emit compoundEditFinished();
}

void SlideCanvasItem::ungroupSelected()
{
    if (m_slide == nullptr || m_selected.isEmpty()) {
        return;
    }
    // Ungrouping one member ungroups the whole group: leaving the others
    // bound together would be a group the user cannot see or reach.
    QSet<QString> groups;
    for (const QString &id : m_selected) {
        const ShowModel::Shape *shape = m_slide->findShape(id);
        if (shape != nullptr && !shape->groupId.isEmpty()) {
            groups.insert(shape->groupId);
        }
    }
    if (groups.isEmpty()) {
        return;
    }
    emit compoundEditStarted();
    for (ShowModel::Shape &shape : m_slide->shapes) {
        if (groups.contains(shape.groupId)) {
            shape.groupId.clear();
        }
    }
    emit slideEdited();
    emit compoundEditFinished();
}

} // namespace QindaOffice::Apps::Show
