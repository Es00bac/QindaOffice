// SPDX-License-Identifier: GPL-3.0-or-later
// Ops (M87, D-095): layers, hyperlinks, comments, background pages, Borders
// & Titles and Auto Size (ops.h).
#include "ops.h"

#include "painter.h"
#include "themes.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DiagramOps", text);
}

bool nameTaken(const Page &page, const QString &name, int except)
{
    for (int i = 0; i < page.layers.size(); ++i) {
        if (i != except && page.layers.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QList<int> remapped(const QList<int> &layers, int removed)
{
    QList<int> out;
    for (const int i : layers) {
        if (i != removed) {
            out.append(i > removed ? i - 1 : i);
        }
    }
    return out;
}

QList<int> cleaned(const Page &page, QList<int> layers)
{
    layers.removeIf([&](int i) { return i < 0 || i >= page.layers.size(); });
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    return layers;
}

// The thread a comment belongs to: its own id, or its parent's.
QString threadOf(const Page &page, const QString &commentId)
{
    for (const Comment &c : page.comments) {
        if (c.id == commentId) {
            return c.parentId.isEmpty() ? c.id : c.parentId;
        }
    }
    return {};
}

} // namespace

int addLayer(Page &page, const QString &name)
{
    QString base = name.trimmed().isEmpty() ? tr("Layer") : name.trimmed();
    QString unique = base;
    for (int n = 2; nameTaken(page, unique, -1); ++n) {
        unique = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    Layer layer;
    layer.name = unique;
    page.layers.append(layer);
    return int(page.layers.size()) - 1;
}

bool removeLayer(Page &page, int index)
{
    if (index < 0 || index >= page.layers.size()) {
        return false;
    }
    page.layers.removeAt(index);
    for (Shape &s : page.shapes) {
        s.layers = remapped(s.layers, index);
    }
    for (Connector &c : page.connectors) {
        c.layers = remapped(c.layers, index);
    }
    return true;
}

bool setLayer(Page &page, int index, const Layer &layer)
{
    if (index < 0 || index >= page.layers.size()) {
        return false;
    }
    Layer next = layer;
    next.name = next.name.trimmed();
    if (next.name.isEmpty() || nameTaken(page, next.name, index)) {
        return false;
    }
    next.extra = page.layers.at(index).extra;
    if (page.layers.at(index) == next) {
        return false;
    }
    page.layers[index] = next;
    return true;
}

bool assignLayers(Page &page, const QStringList &ids, const QList<int> &layers)
{
    const QList<int> wanted = cleaned(page, layers);
    bool changed = false;
    for (Shape &s : page.shapes) {
        if (ids.contains(s.id) && s.layers != wanted) {
            s.layers = wanted;
            changed = true;
        }
    }
    for (Connector &c : page.connectors) {
        if (ids.contains(c.id) && c.layers != wanted) {
            c.layers = wanted;
            changed = true;
        }
    }
    return changed;
}

bool setLinks(Page &page, const QStringList &ids, const QList<Hyperlink> &links)
{
    QList<Hyperlink> kept;
    for (Hyperlink link : links) {
        link.address = link.address.trimmed();
        link.subAddress = link.subAddress.trimmed();
        link.description = link.description.trimmed();
        if (!link.address.isEmpty() || !link.subAddress.isEmpty()) {
            kept.append(link);
        }
    }
    bool changed = false;
    for (Shape &s : page.shapes) {
        if (ids.contains(s.id) && s.links != kept) {
            s.links = kept;
            changed = true;
        }
    }
    for (Connector &c : page.connectors) {
        if (ids.contains(c.id) && c.links != kept) {
            c.links = kept;
            changed = true;
        }
    }
    return changed;
}

QString linkTargetPage(const Drawing &drawing, const Hyperlink &link)
{
    if (!link.address.isEmpty() || link.subAddress.isEmpty()) {
        return {};
    }
    // Visio writes a page as its name, a shape on it as "Page/Shape".
    const QString name = link.subAddress.section(QLatin1Char('/'), 0, 0).trimmed();
    for (const Page &page : drawing.pages) {
        if (page.name.compare(name, Qt::CaseInsensitive) == 0 || page.name.compare(link.subAddress, Qt::CaseInsensitive) == 0) {
            return page.id;
        }
    }
    return {};
}

QString addComment(Drawing &drawing, Page &page, const QString &itemId, const QString &text, const QString &author,
                   const QString &initials, const QDateTime &when, const QString &parentId)
{
    if (text.trimmed().isEmpty() || (!itemId.isEmpty() && !page.contains(itemId))) {
        return {};
    }
    Comment c;
    if (!parentId.isEmpty()) {
        const QString thread = threadOf(page, parentId);
        if (thread.isEmpty()) {
            return {};
        }
        c.parentId = thread;
        for (const Comment &other : std::as_const(page.comments)) {
            if (other.id == thread) {
                c.itemId = other.itemId; // a reply sits where its thread does
            }
        }
    } else {
        c.itemId = itemId;
    }
    c.id = drawing.newId(QStringLiteral("m"));
    c.author = author.trimmed();
    c.initials = initials.trimmed();
    c.date = when.toUTC();
    c.text = text.trimmed();
    page.comments.append(c);
    return c.id;
}

bool editComment(Page &page, const QString &commentId, const QString &text, const QDateTime &when)
{
    Q_UNUSED(when) // Visio's EditDate is kept in `extra` by the reader; the model keeps the first date
    for (Comment &c : page.comments) {
        if (c.id == commentId) {
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty() || trimmed == c.text) {
                return false;
            }
            c.text = trimmed;
            return true;
        }
    }
    return false;
}

bool setCommentDone(Page &page, const QString &commentId, bool done)
{
    const QString thread = threadOf(page, commentId);
    bool changed = false;
    for (Comment &c : page.comments) {
        if (!thread.isEmpty() && (c.id == thread || c.parentId == thread) && c.done != done) {
            c.done = done;
            changed = true;
        }
    }
    return changed;
}

int removeComment(Page &page, const QString &commentId)
{
    const qsizetype before = page.comments.size();
    page.comments.removeIf([&](const Comment &c) { return c.id == commentId || c.parentId == commentId; });
    return int(before - page.comments.size());
}

bool setBackgroundPage(Drawing &drawing, const QString &pageId, bool background)
{
    Page *page = drawing.page(pageId);
    if (page == nullptr || page->backgroundPage == background) {
        return false;
    }
    page->backgroundPage = background;
    if (background) {
        page->backPageId.clear(); // Visio: a background page may have its own background
    } else {
        for (Page &other : drawing.pages) {
            if (other.backPageId == pageId) {
                other.backPageId.clear();
            }
        }
    }
    return true;
}

bool setBackPage(Drawing &drawing, const QString &pageId, const QString &backId)
{
    Page *page = drawing.page(pageId);
    if (page == nullptr || page->backPageId == backId) {
        return false;
    }
    if (!backId.isEmpty()) {
        const Page *back = drawing.page(backId);
        if (back == nullptr || !back->backgroundPage || backId == pageId) {
            return false;
        }
        // No cycles: the chain behind `back` may not reach `pageId`.
        QSet<QString> seen{backId};
        for (QString next = back->backPageId; !next.isEmpty() && !seen.contains(next);) {
            if (next == pageId) {
                return false;
            }
            seen.insert(next);
            const Page *p = drawing.page(next);
            next = p != nullptr ? p->backPageId : QString();
        }
    }
    page->backPageId = backId;
    return true;
}

QString addBackgroundPage(Drawing &drawing, const QString &forPageIdIn)
{
    // AGENT-GUARD: addPage inserts into drawing.pages, which may move every
    // page — a caller's `page.id` reference and `forPage` among them. Work on
    // a copy of the id and re-find pages after the insert.
    const QString forPageId = forPageIdIn;
    const Page *forPage = drawing.page(forPageId);
    if (forPage == nullptr) {
        return {};
    }
    int count = 0;
    for (const Page &p : std::as_const(drawing.pages)) {
        count += p.backgroundPage ? 1 : 0;
    }
    const QString name = QStringLiteral("%1-%2").arg(tr("Background")).arg(count + 1);
    const QString id = addPage(drawing, int(drawing.pages.size()), name, forPage);
    Page *page = drawing.page(id);
    page->backgroundPage = true;
    page->grid = false;
    setBackPage(drawing, forPageId, id);
    return id;
}

bool addBorderAndTitle(Drawing &drawing, const QString &pageIdIn, const QString &title)
{
    // AGENT-GUARD: addBackgroundPage may move every page (addPage inserts),
    // so nothing points into drawing.pages across it — not even the id.
    const QString pageId = pageIdIn;
    const Page *page = drawing.page(pageId);
    if (page == nullptr) {
        return false;
    }
    const QString heading = title.trimmed().isEmpty() ? page->name : title.trimmed();
    QString backId = page->backgroundPage ? pageId : page->backPageId;
    if (backId.isEmpty()) {
        backId = addBackgroundPage(drawing, pageId);
    }
    Page *back = drawing.page(backId);
    if (back == nullptr) {
        return false;
    }
    // A quarter inch in from the paper's edges, a title bar along the top.
    const double inset = 24;
    const QRectF frame(QPointF(inset, inset), back->size - QSizeF(inset * 2, inset * 2));
    const QString border = addShape(drawing, *back, QStringLiteral("rectangle"), frame.center(), QString());
    Shape *b = back->shape(border);
    b->rect = frame;
    b->style.fill = QColor(Qt::transparent);
    b->style.shadow = false;
    const QString caption = addShape(drawing, *back, QStringLiteral("text"), QPointF(frame.center().x(), inset + 26), heading);
    Shape *t = back->shape(caption);
    t->rect = QRectF(frame.left() + 12, inset + 8, frame.width() - 24, 36);
    t->textStyle.size = 16;
    t->textStyle.bold = true;
    t->textStyle.align = Qt::AlignLeft | Qt::AlignVCenter;
    const QString rule = addShape(drawing, *back, QStringLiteral("rectangle"), QPointF(frame.center().x(), inset + 48));
    Shape *r = back->shape(rule);
    r->rect = QRectF(frame.left(), inset + 48, frame.width(), 2);
    r->style.fill = theme(drawing.themeId).text;
    r->style.line = QColor(Qt::transparent);
    r->style.shadow = false;
    return true;
}

bool applyAutoSize(Page &page)
{
    if (!page.autoSize) {
        return false;
    }
    const QSizeF tile = page.print.paper.isEmpty() ? page.size : page.print.paper;
    if (tile.width() < 48 || tile.height() < 48) {
        return false;
    }
    const QRectF content = itemsBounds(page);
    if (content.isNull()) {
        return false;
    }
    // Content past the left or top edge moves in by whole tiles.
    const double dx = content.left() < 0 ? std::ceil(-content.left() / tile.width()) * tile.width() : 0;
    const double dy = content.top() < 0 ? std::ceil(-content.top() / tile.height()) * tile.height() : 0;
    if (dx > 0 || dy > 0) {
        for (Shape &s : page.shapes) {
            s.rect.translate(dx, dy);
        }
        for (Connector &c : page.connectors) {
            c.from.point += QPointF(dx, dy);
            c.to.point += QPointF(dx, dy);
        }
    }
    const QRectF moved = content.translated(dx, dy);
    const QSizeF size(std::max(1.0, std::ceil(moved.right() / tile.width() - 1e-6)) * tile.width(),
                      std::max(1.0, std::ceil(moved.bottom() / tile.height() - 1e-6)) * tile.height());
    if (size == page.size && dx == 0 && dy == 0) {
        return false;
    }
    if (page.print.paper.isEmpty()) {
        // The tile Auto Size keeps growing by is the paper, printed at
        // actual size across as many sheets as the page has tiles (Visio).
        page.print.paper = page.size;
        page.print.zoom = page.print.zoom > 0 ? page.print.zoom : 1.0;
    }
    page.size = size;
    return true;
}

} // namespace QindaOffice::Diagram::Ops
