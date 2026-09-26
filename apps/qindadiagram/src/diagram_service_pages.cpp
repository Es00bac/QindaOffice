// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService: pages and page settings, and the reads the canvas QML
// asks for (hit, glue, ports, text boxes, bounds, routes).
#include "diagram_service.h"

#include "drawing_document.h"
#include "geometry.h"
#include "ops.h"
#include "painter.h"
#include "routing.h"
#include "themes.h"

#include <QSet>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

// ---- pages ------------------------------------------------------------------

QString DiagramService::addPage()
{
    QString id;
    const Page *like = currentPage();
    const Page model = like != nullptr ? *like : Page();
    m_document->edit(tr("New page"), [&](Drawing &d) {
        id = Ops::addPage(d, int(d.pages.size()), QString(), &model);
        return !id.isEmpty();
    });
    setCurrentPageId(id);
    return id;
}

QString DiagramService::duplicatePage()
{
    QString id;
    const QString source = m_pageId;
    m_document->edit(tr("Duplicate page"), [&](Drawing &d) {
        id = Ops::duplicatePage(d, source);
        return !id.isEmpty();
    });
    setCurrentPageId(id);
    return id;
}

bool DiagramService::deletePage(const QString &id)
{
    const QString target = id.isEmpty() ? m_pageId : id;
    const int index = drawing().pageIndex(target);
    if (drawing().pages.size() <= 1) {
        refuse(tr("Delete page"), tr("A drawing keeps at least one page."));
        return false;
    }
    const bool ok = m_document->edit(tr("Delete page"), [&](Drawing &d) { return Ops::removePage(d, target); });
    if (ok && target == m_pageId) {
        setCurrentPageId(drawing().pages.at(std::max(0, index - 1)).id);
    }
    return ok;
}

bool DiagramService::renamePage(const QString &id, const QString &name)
{
    const QString target = id.isEmpty() ? m_pageId : id;
    return m_document->edit(tr("Rename page"), [&](Drawing &d) { return Ops::renamePage(d, target, name); });
}

bool DiagramService::movePage(const QString &id, int delta)
{
    const QString target = id.isEmpty() ? m_pageId : id;
    const int index = drawing().pageIndex(target);
    const bool ok = m_document->edit(tr("Move page"), [&](Drawing &d) { return Ops::movePage(d, target, index + delta); });
    if (ok) {
        emit currentPageChanged();
    }
    return ok;
}

bool DiagramService::setPaper(const QString &paper)
{
    const QSizeF portrait = paperSize(paper);
    const Page *page = currentPage();
    const bool landscape = page != nullptr && page->size.width() > page->size.height();
    return setPageSize(landscape ? portrait.height() : portrait.width(), landscape ? portrait.width() : portrait.height());
}

bool DiagramService::setLandscape(bool landscape)
{
    const Page *page = currentPage();
    if (page == nullptr || (page->size.width() > page->size.height()) == landscape) {
        return false;
    }
    return setPageSize(page->size.height(), page->size.width());
}

bool DiagramService::setPageSize(double width, double height)
{
    if (!(width >= 48 && height >= 48 && width <= 20000 && height <= 20000)) {
        refuse(tr("Page size"), tr("A page is between half an inch and about 200 inches on a side."));
        return false;
    }
    return editPage(tr("Page size"), [&](Drawing &, Page &page) {
        page.size = QSizeF(width, height);
        return true;
    });
}

bool DiagramService::setGridVisible(bool visible)
{
    return editPage(visible ? tr("Show grid") : tr("Hide grid"), [&](Drawing &, Page &page) {
        page.grid = visible;
        return true;
    });
}

bool DiagramService::setLineJumps(bool on)
{
    return editPage(tr("Line jumps"), [&](Drawing &, Page &page) {
        page.lineJumps = on;
        return true;
    });
}

bool DiagramService::setPageBackground(const QString &color)
{
    const QColor c(color);
    if (!c.isValid()) {
        return false;
    }
    return editPage(tr("Page background"), [&](Drawing &, Page &page) {
        page.background = c;
        return true;
    });
}

bool DiagramService::fitPageToContents()
{
    const bool ok = editPage(tr("Fit page to drawing"), [](Drawing &, Page &page) { return Ops::fitPageToContents(page, 36); });
    if (!ok) {
        refuse(tr("Fit page to drawing"), tr("The page is empty."));
    }
    return ok;
}

bool DiagramService::applyTheme(const QString &themeId)
{
    return m_document->edit(tr("Theme"), [&](Drawing &d) { return Diagram::applyTheme(d, themeId); });
}

// ---- reads --------------------------------------------------------------------

QString DiagramService::shapeAt(double x, double y) const
{
    const Page *page = currentPage();
    return page == nullptr ? QString() : Ops::shapeAt(*page, QPointF(x, y));
}

QVariantMap DiagramService::glueAt(double x, double y, double tolerance, const QString &ignore) const
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    const Endpoint end = Ops::glueAt(*page, QPointF(x, y), tolerance, ignore.isEmpty() ? QStringList() : QStringList{ignore});
    QPointF at = end.point;
    if (end.glued() && end.port >= 0) {
        at = portPoint(*page->shape(end.shapeId), end.port);
    }
    return {{QStringLiteral("shapeId"), end.shapeId},
            {QStringLiteral("port"), end.port},
            {QStringLiteral("x"), at.x()},
            {QStringLiteral("y"), at.y()}};
}

QVariantList DiagramService::ports(const QString &shapeId) const
{
    const Page *page = currentPage();
    const Shape *shape = page != nullptr ? page->shape(shapeId) : nullptr;
    QVariantList out;
    if (shape == nullptr) {
        return out;
    }
    for (int i = 0; i < portCount(*shape); ++i) {
        out.append(portPoint(*shape, i));
    }
    return out;
}

QRectF DiagramService::textBox(const QString &id) const
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    if (const Shape *shape = page->shape(id)) {
        return Diagram::textBox(*shape);
    }
    if (const Connector *connector = page->connector(id)) {
        const QPointF at = routeConnector(*page, *connector).labelPoint;
        return QRectF(at - QPointF(70, 14), QSizeF(140, 28));
    }
    return {};
}

QRectF DiagramService::selectionBounds() const
{
    const Page *page = currentPage();
    if (page == nullptr || m_selection.isEmpty()) {
        return {};
    }
    return itemsBounds(*page, QSet<QString>(m_selection.begin(), m_selection.end()));
}

QRectF DiagramService::contentBounds() const
{
    const Page *page = currentPage();
    return page == nullptr ? QRectF() : itemsBounds(*page);
}

QVariantList DiagramService::route(const QString &connectorId) const
{
    const Page *page = currentPage();
    const Connector *connector = page != nullptr ? page->connector(connectorId) : nullptr;
    QVariantList out;
    if (connector != nullptr) {
        for (const QPointF &p : routePolyline(routeConnector(*page, *connector))) {
            out.append(p);
        }
    }
    return out;
}

void DiagramService::selectAll()
{
    const Page *page = currentPage();
    QStringList ids;
    if (page != nullptr) {
        for (const Shape &shape : page->shapes) {
            ids.append(shape.id);
        }
        for (const Connector &connector : page->connectors) {
            ids.append(connector.id);
        }
    }
    setSelection(ids);
}

void DiagramService::selectAndReveal(const QStringList &ids)
{
    setSelection(ids);
    const QRectF bounds = selectionBounds();
    if (!bounds.isNull()) {
        emit revealRequested(bounds);
    }
}

} // namespace QindaOffice::Apps::Diagram
