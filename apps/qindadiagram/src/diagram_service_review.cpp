// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService (M87, D-095): layers, hyperlinks, comments, background
// pages, Auto Size, line jumps and print setup — each call one undo step.
#include "diagram_service.h"

#include "drawing_document.h"
#include "geometry.h"
#include "layers.h"
#include "ops.h"
#include "print_tiles.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QLocale>
#include <QSet>

#include <algorithm>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

QString authorName()
{
    // As QindaShow and QindaWrite name a comment's author (D-084).
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return user.isEmpty() ? QCoreApplication::translate("DiagramService", "Author") : user;
}

QString initialsOf(const QString &name)
{
    QString out;
    for (const QString &part : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out.append(part.at(0).toUpper());
    }
    return out.left(3);
}

QString colorName(const QColor &c)
{
    return c.isValid() && c.alpha() > 0 ? c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb) : QString();
}

QString itemTitle(const Page &page, const QString &id)
{
    if (const Shape *s = page.shape(id)) {
        const QString first = s->text.section(QLatin1Char('\n'), 0, 0).trimmed();
        return first.isEmpty() ? QCoreApplication::translate("DiagramService", "a shape") : first;
    }
    if (page.connector(id) != nullptr) {
        return QCoreApplication::translate("DiagramService", "a connector");
    }
    return QCoreApplication::translate("DiagramService", "this page");
}

} // namespace

// ---- layers ---------------------------------------------------------------

QVariantList DiagramService::layerList() const
{
    QVariantList out;
    const Page *page = currentPage();
    if (page == nullptr) {
        return out;
    }
    for (int i = 0; i < page->layers.size(); ++i) {
        const Layer &l = page->layers.at(i);
        int count = 0;
        for (const Shape &s : page->shapes) {
            count += s.layers.contains(i) ? 1 : 0;
        }
        for (const Connector &c : page->connectors) {
            count += c.layers.contains(i) ? 1 : 0;
        }
        out.append(QVariantMap{{QStringLiteral("index"), i},          {QStringLiteral("name"), l.name},
                               {QStringLiteral("visible"), l.visible}, {QStringLiteral("print"), l.print},
                               {QStringLiteral("locked"), l.locked},   {QStringLiteral("active"), l.active},
                               {QStringLiteral("glue"), l.glue},       {QStringLiteral("color"), colorName(l.color)},
                               {QStringLiteral("count"), count}});
    }
    return out;
}

int DiagramService::addLayer(const QString &name)
{
    int index = -1;
    editPage(tr("New layer"), [&](Drawing &, Page &page) {
        index = Ops::addLayer(page, name);
        return index >= 0;
    });
    return index;
}

bool DiagramService::removeLayer(int index)
{
    return editPage(tr("Remove layer"), [&](Drawing &, Page &page) { return Ops::removeLayer(page, index); });
}

bool DiagramService::setLayerProperty(int index, const QString &key, const QVariant &value)
{
    const Page *page = currentPage();
    if (page == nullptr || index < 0 || index >= page->layers.size()) {
        return false;
    }
    Layer next = page->layers.at(index);
    if (key == QLatin1String("name")) next.name = value.toString();
    else if (key == QLatin1String("visible")) next.visible = value.toBool();
    else if (key == QLatin1String("print")) next.print = value.toBool();
    else if (key == QLatin1String("locked")) next.locked = value.toBool();
    else if (key == QLatin1String("active")) next.active = value.toBool();
    else if (key == QLatin1String("glue")) next.glue = value.toBool();
    else if (key == QLatin1String("color")) next.color = value.toString().isEmpty() ? QColor() : QColor(value.toString());
    else return false;
    const bool ok = editPage(tr("Layer properties"), [&](Drawing &, Page &p) { return Ops::setLayer(p, index, next); });
    if (!ok && key == QLatin1String("name")) {
        refuse(tr("Layers"), tr("A layer needs a name no other layer on this page has."));
    }
    return ok;
}

QVariantList DiagramService::selectionLayers() const
{
    QVariantList out;
    const Page *page = currentPage();
    if (page == nullptr || m_selection.isEmpty()) {
        return out;
    }
    const QString first = m_selection.first();
    const QList<int> layers = page->shape(first) != nullptr ? page->shape(first)->layers
                            : page->connector(first) != nullptr ? page->connector(first)->layers
                                                                : QList<int>();
    for (const int i : layers) {
        out.append(i);
    }
    return out;
}

bool DiagramService::assignSelectionToLayers(const QVariantList &indexes)
{
    QList<int> layers;
    for (const QVariant &v : indexes) {
        layers.append(v.toInt());
    }
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        refuse(tr("Assign to layer"), tr("Select the shapes to put on a layer first."));
        return false;
    }
    return editPage(tr("Assign to layer"), [&](Drawing &, Page &page) { return Ops::assignLayers(page, ids, layers); });
}

// ---- hyperlinks -----------------------------------------------------------

QVariantList DiagramService::links(const QString &itemId) const
{
    QVariantList out;
    const Page *page = currentPage();
    if (page == nullptr) {
        return out;
    }
    const QList<Hyperlink> list = page->shape(itemId) != nullptr ? page->shape(itemId)->links
                                : page->connector(itemId) != nullptr ? page->connector(itemId)->links
                                                                      : QList<Hyperlink>();
    for (const Hyperlink &l : list) {
        out.append(QVariantMap{{QStringLiteral("description"), l.description},
                               {QStringLiteral("address"), l.address},
                               {QStringLiteral("subAddress"), l.subAddress}});
    }
    return out;
}

bool DiagramService::setSelectionLinks(const QVariantList &list)
{
    QList<Hyperlink> links;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        links.append({m.value(QStringLiteral("description")).toString(), m.value(QStringLiteral("address")).toString(),
                      m.value(QStringLiteral("subAddress")).toString()});
    }
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        refuse(tr("Link"), tr("Select the shape to link first."));
        return false;
    }
    return editPage(tr("Link"), [&](Drawing &, Page &page) { return Ops::setLinks(page, ids, links); });
}

bool DiagramService::followLink(const QString &itemId)
{
    const QVariantList list = links(itemId);
    if (list.isEmpty()) {
        return false;
    }
    const QVariantMap first = list.first().toMap();
    const Hyperlink link{first.value(QStringLiteral("description")).toString(),
                         first.value(QStringLiteral("address")).toString(),
                         first.value(QStringLiteral("subAddress")).toString()};
    const QString pageId = Ops::linkTargetPage(drawing(), link);
    if (!pageId.isEmpty()) {
        setCurrentPageId(pageId);
        return true;
    }
    if (link.address.isEmpty()) {
        refuse(tr("Link"), tr("This drawing has no page named \"%1\".").arg(link.subAddress));
        return false;
    }
    const QUrl url = QUrl::fromUserInput(link.address);
    if (!url.isValid() || !QDesktopServices::openUrl(url)) {
        refuse(tr("Link"), tr("Could not open %1.").arg(link.address));
        return false;
    }
    return true;
}

// ---- comments -------------------------------------------------------------

QVariantList DiagramService::commentList() const
{
    QVariantList out;
    const Page *page = currentPage();
    if (page == nullptr) {
        return out;
    }
    const QLocale locale;
    const auto entry = [&](const Comment &c) {
        return QVariantMap{{QStringLiteral("id"), c.id},
                           {QStringLiteral("author"), c.author},
                           {QStringLiteral("initials"), c.initials},
                           {QStringLiteral("date"), c.date.isValid() ? locale.toString(c.date.toLocalTime(), QLocale::ShortFormat)
                                                                     : QString()},
                           {QStringLiteral("text"), c.text},
                           {QStringLiteral("done"), c.done},
                           {QStringLiteral("itemId"), c.itemId}};
    };
    for (const Comment &c : page->comments) {
        if (!c.parentId.isEmpty()) {
            continue;
        }
        QVariantMap thread = entry(c);
        thread.insert(QStringLiteral("anchor"), itemTitle(*page, c.itemId));
        QVariantList replies;
        for (const Comment &r : page->comments) {
            if (r.parentId == c.id) {
                replies.append(entry(r));
            }
        }
        thread.insert(QStringLiteral("replies"), replies);
        out.append(thread);
    }
    return out;
}

int DiagramService::commentTotal() const
{
    int total = 0;
    for (const Page &page : drawing().pages) {
        for (const Comment &c : page.comments) {
            total += c.parentId.isEmpty() ? 1 : 0;
        }
    }
    return total;
}

void DiagramService::setActiveComment(const QString &id)
{
    if (id == m_activeComment) {
        return;
    }
    m_activeComment = id;
    emit activeCommentChanged();
    // The comment's item becomes the selection, as clicking its marker does.
    const Page *page = currentPage();
    for (const Comment &c : page != nullptr ? page->comments : QVector<Comment>()) {
        if (c.id == id && !c.itemId.isEmpty() && page->contains(c.itemId)) {
            selectAndReveal({c.itemId});
        }
    }
}

QString DiagramService::addComment(const QString &text, const QString &parentId)
{
    QString id;
    const QString item = parentId.isEmpty() && !m_selection.isEmpty() ? m_selection.first() : QString();
    const QString author = authorName();
    editPage(parentId.isEmpty() ? tr("New comment") : tr("Reply"), [&](Drawing &d, Page &page) {
        id = Ops::addComment(d, page, item, text, author, initialsOf(author), QDateTime::currentDateTimeUtc(), parentId);
        return !id.isEmpty();
    });
    if (id.isEmpty()) {
        refuse(tr("Comments"), tr("A comment needs some text."));
    } else if (parentId.isEmpty()) {
        m_activeComment = id;
        emit activeCommentChanged();
    }
    return id;
}

bool DiagramService::editComment(const QString &id, const QString &text)
{
    return editPage(tr("Edit comment"), [&](Drawing &, Page &page) {
        return Ops::editComment(page, id, text, QDateTime::currentDateTimeUtc());
    });
}

bool DiagramService::setCommentDone(const QString &id, bool done)
{
    return editPage(done ? tr("Resolve comment") : tr("Reopen comment"),
                    [&](Drawing &, Page &page) { return Ops::setCommentDone(page, id, done); });
}

bool DiagramService::deleteComment(const QString &id)
{
    const QString target = id.isEmpty() ? m_activeComment : id;
    const bool ok = !target.isEmpty()
        && editPage(tr("Delete comment"), [&](Drawing &, Page &page) { return Ops::removeComment(page, target) > 0; });
    if (ok && target == m_activeComment) {
        m_activeComment.clear();
        emit activeCommentChanged();
    }
    return ok;
}

QString DiagramService::stepComment(int step)
{
    // Every thread of the drawing in page order; the walk wraps.
    QList<QPair<QString, QString>> threads; // (page id, comment id)
    for (const Page &page : drawing().pages) {
        for (const Comment &c : page.comments) {
            if (c.parentId.isEmpty()) {
                threads.append({page.id, c.id});
            }
        }
    }
    if (threads.isEmpty()) {
        return {};
    }
    qsizetype at = -1;
    for (qsizetype i = 0; i < threads.size(); ++i) {
        if (threads.at(i).second == m_activeComment) {
            at = i;
        }
    }
    const qsizetype count = threads.size();
    const qsizetype next = at < 0 ? (step >= 0 ? 0 : count - 1) : ((at + step) % count + count) % count;
    setCurrentPageId(threads.at(next).first);
    setActiveComment(threads.at(next).second);
    return threads.at(next).second;
}

QVariantList DiagramService::commentMarkers() const
{
    QVariantList out;
    const Page *page = currentPage();
    if (page == nullptr) {
        return out;
    }
    QSet<QString> placed;
    for (const Comment &c : page->comments) {
        if (!c.parentId.isEmpty() || placed.contains(c.itemId)) {
            continue;
        }
        placed.insert(c.itemId);
        QPointF at(12, 12); // the page's own comments: its top-left corner
        if (const Shape *s = page->shape(c.itemId)) {
            if (!layersVisible(*page, s->layers)) {
                continue;
            }
            const QRectF b = shapeBounds(*s);
            at = QPointF(b.right(), b.top());
        } else if (page->connector(c.itemId) != nullptr) {
            const QVariantList points = route(c.itemId);
            at = points.isEmpty() ? at : points.at(points.size() / 2).toPointF();
        }
        out.append(QVariantMap{{QStringLiteral("id"), c.id}, {QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()},
                               {QStringLiteral("done"), c.done}});
    }
    return out;
}

// ---- background pages, Auto Size, line jumps, print setup -----------------

QVariantList DiagramService::backgroundPageList() const
{
    QVariantList out;
    for (const Page &page : drawing().pages) {
        if (page.backgroundPage) {
            out.append(QVariantMap{{QStringLiteral("id"), page.id}, {QStringLiteral("name"), page.name}});
        }
    }
    return out;
}

bool DiagramService::setBackgroundPage(bool background)
{
    const QString id = m_pageId;
    return m_document->edit(background ? tr("Make background page") : tr("Make foreground page"),
                            [&](Drawing &d) { return Ops::setBackgroundPage(d, id, background); });
}

bool DiagramService::setBackPage(const QString &backId)
{
    const QString id = m_pageId;
    const bool ok = m_document->edit(tr("Background"), [&](Drawing &d) { return Ops::setBackPage(d, id, backId); });
    if (!ok && !backId.isEmpty() && drawing().page(id) != nullptr && drawing().page(id)->backPageId != backId) {
        refuse(tr("Background"), tr("Only a background page can be shown behind a page, and never behind itself."));
    }
    return ok;
}

QString DiagramService::addBackgroundPage()
{
    QString id;
    const QString forPage = m_pageId;
    m_document->edit(tr("New background page"), [&](Drawing &d) {
        id = Ops::addBackgroundPage(d, forPage);
        return !id.isEmpty();
    });
    return id;
}

bool DiagramService::addBorderAndTitle()
{
    const QString pageId = m_pageId;
    const QString title = drawing().title.isEmpty() ? QString() : drawing().title;
    return m_document->edit(tr("Borders & Titles"), [&](Drawing &d) { return Ops::addBorderAndTitle(d, pageId, title); });
}

bool DiagramService::setAutoSize(bool on)
{
    return editPage(tr("Auto Size"), [&](Drawing &, Page &page) {
        if (page.autoSize == on) {
            return false;
        }
        page.autoSize = on;
        return true;
    });
}

bool DiagramService::setLineJumpStyle(const QString &style, const QString &on)
{
    return editPage(tr("Line jumps"), [&](Drawing &, Page &page) {
        const JumpStyle s = style == QLatin1String("gap") ? JumpStyle::Gap
                          : style == QLatin1String("square") ? JumpStyle::Square
                                                             : JumpStyle::Arc;
        const JumpOn o = on == QLatin1String("horizontal") ? JumpOn::Horizontal
                       : on == QLatin1String("vertical")   ? JumpOn::Vertical
                                                           : JumpOn::LastRouted;
        if (page.jumpStyle == s && page.jumpOn == o && page.lineJumps) {
            return false;
        }
        page.jumpStyle = s;
        page.jumpOn = o;
        page.lineJumps = true; // choosing a style turns jumps on
        return true;
    });
}

bool DiagramService::setPrintSetup(const QVariantMap &setup)
{
    return editPage(tr("Print setup"), [&](Drawing &, Page &page) {
        PrintSetup p = page.print;
        const QString paper = setup.value(QStringLiteral("paper"), QString()).toString();
        if (setup.contains(QStringLiteral("paper"))) {
            p.paper = paper.isEmpty() ? QSizeF() : paper == QLatin1String("page") ? page.size : paperSize(paper);
            const bool landscape = setup.value(QStringLiteral("landscape"), page.size.width() > page.size.height()).toBool();
            if (!p.paper.isEmpty() && landscape != (p.paper.width() > p.paper.height())) {
                p.paper.transpose();
            }
        }
        if (setup.contains(QStringLiteral("zoom"))) p.zoom = std::clamp(setup.value(QStringLiteral("zoom")).toDouble(), 0.0, 10.0);
        if (setup.contains(QStringLiteral("across"))) p.sheetsAcross = std::clamp(setup.value(QStringLiteral("across")).toInt(), 1, 50);
        if (setup.contains(QStringLiteral("down"))) p.sheetsDown = std::clamp(setup.value(QStringLiteral("down")).toInt(), 1, 50);
        if (setup.contains(QStringLiteral("center"))) p.center = setup.value(QStringLiteral("center")).toBool();
        if (p == page.print) {
            return false;
        }
        page.print = p;
        return true;
    });
}

QVariantList DiagramService::pageBreaks() const
{
    QVariantList out;
    if (const Page *page = currentPage()) {
        for (const QRectF &r : pageBreakTiles(*page)) {
            out.append(r);
        }
    }
    return out;
}

} // namespace QindaOffice::Apps::Diagram
