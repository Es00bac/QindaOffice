// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService (M87, D-095): containers, swimlanes, AutoConnect and
// Re-Layout — each call one undo step (containers then settle through
// editPage, diagram_service.h).
#include "diagram_service.h"

#include "drawing_document.h"
#include "layout.h"
#include "ops.h"
#include "stencils.h"

#include <cmath>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

QStringList containersIn(const Page *page, const QStringList &ids)
{
    QStringList out;
    for (const QString &id : ids) {
        const Shape *s = page != nullptr ? page->shape(id) : nullptr;
        if (s != nullptr && isContainer(*s)) {
            out.append(id);
        }
    }
    return out;
}

} // namespace

QString DiagramService::insertSwimlane(const QString &type, bool before, double x, double y)
{
    // The lane chosen: the selected lane of that orientation, if any.
    QString lane;
    const Page *page = currentPage();
    for (const QString &id : std::as_const(m_selection)) {
        const Shape *s = page != nullptr ? page->shape(id) : nullptr;
        if (s != nullptr && Ops::isLane(*s)
            && Ops::laneOrientation(*s) == (type == QLatin1String("laneVertical") ? Qt::Vertical : Qt::Horizontal)) {
            lane = id;
            break;
        }
    }
    QString id;
    editPage(tr("Insert swimlane"), [&](Drawing &d, Page &p) {
        id = Ops::insertLane(d, p, lane, !before, type, QPointF(x, y));
        return !id.isEmpty();
    });
    if (!id.isEmpty()) {
        setSelection({id});
    }
    return id;
}

QString DiagramService::insertPhase(double x, double y)
{
    QString id;
    editPage(tr("Insert phase"), [&](Drawing &d, Page &p) {
        id = Ops::insertPhase(d, p, QPointF(x, y));
        return !id.isEmpty();
    });
    if (!id.isEmpty()) {
        setSelection({id});
    }
    return id;
}

QString DiagramService::insertContainer(const QString &type, double x, double y)
{
    // Visio's Insert ▸ Container wraps what is selected (shapes only).
    QStringList shapes;
    if (const Page *page = currentPage()) {
        for (const QString &id : std::as_const(m_selection)) {
            if (page->shape(id) != nullptr) {
                shapes.append(id);
            }
        }
    }
    if (shapes.isEmpty()) {
        return addShape(type, x, y);
    }
    QString id;
    editPage(tr("Add to new container"), [&](Drawing &d, Page &p) {
        id = Ops::wrapInContainer(d, p, shapes, type);
        return !id.isEmpty();
    });
    if (!id.isEmpty()) {
        setSelection({id});
    }
    return id;
}

bool DiagramService::fitContainers()
{
    const QStringList ids = containersIn(currentPage(), m_selection);
    const bool ok = !ids.isEmpty()
        && editPage(tr("Fit to contents"), [&](Drawing &, Page &page) { return Ops::fitToContents(page, ids); });
    if (!ok && ids.isEmpty()) {
        refuse(tr("Container"), tr("Select a container or a swimlane first."));
    }
    return ok;
}

bool DiagramService::disbandContainers()
{
    const QStringList ids = containersIn(currentPage(), m_selection);
    const bool ok = !ids.isEmpty()
        && editPage(tr("Disband container"), [&](Drawing &, Page &page) { return Ops::disband(page, ids); });
    if (!ok && ids.isEmpty()) {
        refuse(tr("Container"), tr("Select a container or a swimlane first."));
    }
    if (ok) {
        setSelection({});
    }
    return ok;
}

bool DiagramService::setContainerResize(const QString &resize)
{
    const QStringList ids = containersIn(currentPage(), m_selection);
    const ContainerResize value = resize == QLatin1String("fit")    ? ContainerResize::Fit
                                : resize == QLatin1String("none")   ? ContainerResize::None
                                                                    : ContainerResize::Expand;
    return !ids.isEmpty() && editPage(tr("Container resize"), [&](Drawing &, Page &page) {
        const bool changed = Ops::setContainerResize(page, ids, value);
        // "Always fit" fits now, as Visio does when it is chosen.
        if (value == ContainerResize::Fit) {
            Ops::fitToContents(page, ids);
        }
        return changed;
    });
}

bool DiagramService::setContainerMargin(double margin)
{
    const QStringList ids = containersIn(currentPage(), m_selection);
    return !ids.isEmpty() && editPage(tr("Container margin"), [&](Drawing &, Page &page) {
        const bool changed = Ops::setContainerMargin(page, ids, margin);
        for (const QString &id : ids) {
            if (const Shape *s = page.shape(id); s != nullptr && Ops::effectiveResize(*s) == ContainerResize::Fit) {
                Ops::fitToContents(page, {id});
            }
        }
        return changed;
    }, QStringLiteral("containerMargin"));
}

bool DiagramService::selectContainerContents()
{
    const Page *page = currentPage();
    QStringList members;
    for (const QString &id : containersIn(page, m_selection)) {
        members += Ops::containerMembers(*page, id);
    }
    if (members.isEmpty()) {
        refuse(tr("Container"), tr("The container is empty."));
        return false;
    }
    setSelection(members);
    return true;
}

QVariantMap DiagramService::containerInfo() const
{
    const Page *page = currentPage();
    const QStringList ids = containersIn(page, m_selection);
    if (ids.isEmpty()) {
        return {};
    }
    const Shape &c = *page->shape(ids.first());
    const ContainerResize resize = Ops::effectiveResize(c);
    return {{QStringLiteral("resize"), resize == ContainerResize::Fit    ? QStringLiteral("fit")
                                       : resize == ContainerResize::None ? QStringLiteral("none")
                                                                         : QStringLiteral("expand")},
            {QStringLiteral("margin"), Ops::containerMargin(c)},
            {QStringLiteral("lane"), Ops::isLane(c)},
            {QStringLiteral("members"), int(Ops::containerMembers(*page, c.id).size())}};
}

void DiagramService::setQuickShape(const QString &stencilId)
{
    const QString next = Diagram::stencil(stencilId) != nullptr ? stencilId : QString();
    if (next == m_quickShape) {
        return;
    }
    m_quickShape = next;
    emit quickShapeChanged();
}

QString DiagramService::autoConnect(const QString &fromId, int side)
{
    const Page *page = currentPage();
    if (page == nullptr || page->shape(fromId) == nullptr) {
        refuse(tr("AutoConnect"), tr("Select the shape to connect from first."));
        return {};
    }
    QString target;
    QString connector;
    editPage(tr("AutoConnect"), [&](Drawing &d, Page &p) {
        target = Ops::autoConnect(d, p, fromId, side, m_quickShape, 48, &connector);
        return !target.isEmpty();
    });
    if (!target.isEmpty()) {
        selectAndReveal({target});
    }
    return target;
}

bool DiagramService::relayout(const QVariantMap &options)
{
    LayoutOptions o;
    o.kind = layoutKindOf(options.value(QStringLiteral("kind")).toString(), LayoutKind::TopDown);
    const double rank = options.value(QStringLiteral("rankGap"), o.rankGap).toDouble();
    const double node = options.value(QStringLiteral("nodeGap"), o.nodeGap).toDouble();
    o.rankGap = std::isfinite(rank) ? std::clamp(rank, 8.0, 960.0) : o.rankGap;
    o.nodeGap = std::isfinite(node) ? std::clamp(node, 4.0, 960.0) : o.nodeGap;
    QStringList ids;
    for (const QString &id : std::as_const(m_selection)) {
        if (currentPage() != nullptr && currentPage()->shapeIndex(id) >= 0) {
            ids.append(id);
        }
    }
    QString error;
    const bool ok = editPage(tr("Re-layout"), [&](Drawing &, Page &page) { return Diagram::autoLayout(page, ids, o, &error); });
    if (!ok) {
        refuse(tr("Re-layout"), error.isEmpty() ? tr("The shapes are already arranged.") : error);
    }
    return ok;
}

} // namespace QindaOffice::Apps::Diagram
