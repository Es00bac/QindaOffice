// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService: pages, selection, tool state and the reads QML binds to.
#include "diagram_service.h"

#include "drawing_document.h"
#include "geometry.h"
#include "ops.h"
#include "painter.h"
#include "routing.h"
#include "stencils.h"
#include "themes.h"

#include <QBuffer>
#include <QClipboard>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

QString colorName(const QColor &color)
{
    if (!color.isValid() || color.alpha() == 0) {
        return {};
    }
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QString patternName(LinePattern pattern)
{
    switch (pattern) {
    case LinePattern::Dash: return QStringLiteral("dash");
    case LinePattern::Dot: return QStringLiteral("dot");
    case LinePattern::DashDot: return QStringLiteral("dashDot");
    case LinePattern::Solid: break;
    }
    return QStringLiteral("solid");
}

QString arrowName(ArrowStyle arrow)
{
    switch (arrow) {
    case ArrowStyle::Filled: return QStringLiteral("filled");
    case ArrowStyle::Open: return QStringLiteral("open");
    case ArrowStyle::Diamond: return QStringLiteral("diamond");
    case ArrowStyle::Circle: return QStringLiteral("circle");
    case ArrowStyle::None: break;
    }
    return QStringLiteral("none");
}

QString routingName(Routing routing)
{
    switch (routing) {
    case Routing::Straight: return QStringLiteral("straight");
    case Routing::Curved: return QStringLiteral("curved");
    case Routing::Orthogonal: break;
    }
    return QStringLiteral("orthogonal");
}

QVariantMap textMap(const TextStyle &t)
{
    const Qt::Alignment h = t.align & Qt::AlignHorizontal_Mask;
    const Qt::Alignment v = t.align & Qt::AlignVertical_Mask;
    return {{QStringLiteral("family"), t.family},
            {QStringLiteral("size"), t.size},
            {QStringLiteral("bold"), t.bold},
            {QStringLiteral("italic"), t.italic},
            {QStringLiteral("underline"), t.underline},
            {QStringLiteral("color"), colorName(t.color)},
            {QStringLiteral("align"), h & Qt::AlignLeft ? QStringLiteral("left") : h & Qt::AlignRight ? QStringLiteral("right") : QStringLiteral("center")},
            {QStringLiteral("valign"), v & Qt::AlignTop ? QStringLiteral("top") : v & Qt::AlignBottom ? QStringLiteral("bottom") : QStringLiteral("middle")}};
}

QVariantList dataList(const QList<DataField> &data)
{
    QVariantList out;
    for (const DataField &field : data) {
        out.append(QVariantMap{{QStringLiteral("key"), field.key}, {QStringLiteral("value"), field.value}});
    }
    return out;
}

QString sizeText(QSizeF size)
{
    // Inches for letter-family paper, millimetres for ISO (the paper's own unit).
    const QString paper = paperNameOf(size);
    if (paper.startsWith(QLatin1Char('a'))) {
        return QCoreApplication::translate("DiagramService", "%1 × %2 mm")
            .arg(qRound(size.width() * 25.4 / 96))
            .arg(qRound(size.height() * 25.4 / 96));
    }
    return QCoreApplication::translate("DiagramService", "%1 × %2 in")
        .arg(size.width() / 96, 0, 'g', 3)
        .arg(size.height() / 96, 0, 'g', 3);
}

} // namespace

DiagramService::DiagramService(DrawingDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    m_pageId = drawing().pages.isEmpty() ? QString() : drawing().pages.first().id;
    connect(m_document, &DrawingDocument::drawingChanged, this, &DiagramService::documentChanged);
    connect(m_document, &DrawingDocument::drawingReset, this, [this] {
        m_pageId = drawing().pages.isEmpty() ? QString() : drawing().pages.first().id;
        m_selection.clear();
        emit currentPageChanged();
        emit selectionChanged();
        emit selectedChanged();
        emit stencilsChanged();
    });
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, &DiagramService::clipboardChanged);
    }
}

const Drawing &DiagramService::drawing() const
{
    return m_document->drawing();
}

const Page *DiagramService::currentPage() const
{
    return drawing().page(m_pageId);
}

void DiagramService::documentChanged()
{
    ++m_revision;
    // Undo may remove the current page or items: fall back and prune.
    if (drawing().page(m_pageId) == nullptr && !drawing().pages.isEmpty()) {
        m_pageId = drawing().pages.first().id;
        emit currentPageChanged();
    }
    if (m_stencilTheme != drawing().themeId) {
        emit stencilsChanged();
    }
    pruneSelection();
    emit changed();
    emit pageInfoChanged();
    emit selectedChanged();
}

bool DiagramService::editPage(const QString &label, const std::function<bool(Drawing &, Page &)> &change,
                              const QString &mergeKey)
{
    const QString pageId = m_pageId;
    return m_document->edit(
        label,
        [&](Drawing &d) {
            Page *page = d.page(pageId);
            return page != nullptr && change(d, *page);
        },
        mergeKey);
}

void DiagramService::refuse(const QString &title, const QString &text)
{
    emit notify(title, text, QStringLiteral("warning"));
}

QVariantList DiagramService::pages() const
{
    QVariantList out;
    for (int i = 0; i < drawing().pages.size(); ++i) {
        const Page &page = drawing().pages.at(i);
        out.append(QVariantMap{{QStringLiteral("id"), page.id},
                               {QStringLiteral("name"), page.name},
                               {QStringLiteral("index"), i},
                               {QStringLiteral("shapeCount"), int(page.shapes.size())}});
    }
    return out;
}

void DiagramService::setCurrentPageId(const QString &id)
{
    if (id == m_pageId || drawing().page(id) == nullptr) {
        return;
    }
    m_pageId = id;
    m_selection.clear();
    setEditingTextId({});
    emit currentPageChanged();
    emit pageInfoChanged();
    emit selectionChanged();
    emit selectedChanged();
}

int DiagramService::currentPageIndex() const
{
    return drawing().pageIndex(m_pageId);
}

QVariantMap DiagramService::pageInfo() const
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    return {{QStringLiteral("id"), page->id},
            {QStringLiteral("name"), page->name},
            {QStringLiteral("width"), page->size.width()},
            {QStringLiteral("height"), page->size.height()},
            {QStringLiteral("landscape"), page->size.width() > page->size.height()},
            {QStringLiteral("paper"), paperNameOf(page->size)},
            {QStringLiteral("sizeText"), sizeText(page->size)},
            {QStringLiteral("grid"), page->grid},
            {QStringLiteral("gridSpacing"), page->gridSpacing},
            {QStringLiteral("lineJumps"), page->lineJumps},
            {QStringLiteral("background"), colorName(page->background)},
            {QStringLiteral("shapeCount"), int(page->shapes.size())},
            {QStringLiteral("connectorCount"), int(page->connectors.size())}};
}

void DiagramService::pruneSelection()
{
    const Page *page = currentPage();
    QStringList kept;
    for (const QString &id : std::as_const(m_selection)) {
        if (page != nullptr && page->contains(id)) {
            kept.append(id);
        }
    }
    if (kept != m_selection) {
        m_selection = kept;
        emit selectionChanged();
    }
    if (!m_editingTextId.isEmpty() && (page == nullptr || !page->contains(m_editingTextId))) {
        setEditingTextId({});
    }
}

void DiagramService::setSelection(const QStringList &ids)
{
    const Page *page = currentPage();
    QStringList next;
    for (const QString &id : ids) {
        if (page != nullptr && page->contains(id) && !next.contains(id)) {
            next.append(id);
        }
    }
    if (page != nullptr) {
        next = Ops::expandGroups(*page, next);
    }
    if (next == m_selection) {
        return;
    }
    m_selection = next;
    emit selectionChanged();
    emit selectedChanged();
}

QString DiagramService::selectionKind() const
{
    const Page *page = currentPage();
    if (page == nullptr || m_selection.isEmpty()) {
        return QStringLiteral("none");
    }
    bool shapes = false;
    bool connectors = false;
    for (const QString &id : m_selection) {
        shapes = shapes || page->shapeIndex(id) >= 0;
        connectors = connectors || page->connectorIndex(id) >= 0;
    }
    return shapes && connectors ? QStringLiteral("mixed") : shapes ? QStringLiteral("shape") : QStringLiteral("connector");
}

QVariantMap DiagramService::selected() const
{
    return m_selection.isEmpty() ? QVariantMap() : itemMap(m_selection.first());
}

QVariantMap DiagramService::item(const QString &id) const
{
    return itemMap(id);
}

QVariantMap DiagramService::itemMap(const QString &id) const
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    if (const Shape *s = page->shape(id)) {
        const Stencil *stencil = Diagram::stencil(s->type);
        return {{QStringLiteral("id"), s->id},
                {QStringLiteral("kind"), QStringLiteral("shape")},
                {QStringLiteral("type"), s->type},
                {QStringLiteral("typeTitle"), stencil != nullptr ? stencil->title : s->type},
                {QStringLiteral("container"), isContainer(*s)},
                {QStringLiteral("text"), s->text},
                {QStringLiteral("x"), s->rect.x()},
                {QStringLiteral("y"), s->rect.y()},
                {QStringLiteral("width"), s->rect.width()},
                {QStringLiteral("height"), s->rect.height()},
                {QStringLiteral("rotation"), s->rotation},
                {QStringLiteral("flipX"), s->flipX},
                {QStringLiteral("flipY"), s->flipY},
                {QStringLiteral("locked"), s->locked},
                {QStringLiteral("aspectLocked"), s->aspectLocked},
                {QStringLiteral("groupId"), s->groupId},
                {QStringLiteral("fill"), colorName(s->style.fill)},
                {QStringLiteral("gradient"), colorName(s->style.gradient)},
                {QStringLiteral("line"), colorName(s->style.line)},
                {QStringLiteral("lineWidth"), s->style.lineWidth},
                {QStringLiteral("pattern"), patternName(s->style.pattern)},
                {QStringLiteral("corner"), s->style.cornerRadius},
                {QStringLiteral("shadow"), s->style.shadow},
                {QStringLiteral("opacity"), s->style.opacity},
                {QStringLiteral("textStyle"), textMap(s->textStyle)},
                {QStringLiteral("data"), dataList(s->data)}};
    }
    if (const Connector *c = page->connector(id)) {
        const auto title = [&](const Endpoint &e) {
            const Shape *s = e.glued() ? page->shape(e.shapeId) : nullptr;
            return s == nullptr ? QString() : s->text.section(QLatin1Char('\n'), 0, 0);
        };
        return {{QStringLiteral("id"), c->id},
                {QStringLiteral("kind"), QStringLiteral("connector")},
                {QStringLiteral("typeTitle"), QCoreApplication::translate("DiagramService", "Connector")},
                {QStringLiteral("text"), c->text},
                {QStringLiteral("routing"), routingName(c->routing)},
                {QStringLiteral("startArrow"), arrowName(c->startArrow)},
                {QStringLiteral("endArrow"), arrowName(c->endArrow)},
                {QStringLiteral("labelPosition"), c->labelPosition},
                {QStringLiteral("line"), colorName(c->line)},
                {QStringLiteral("lineWidth"), c->lineWidth},
                {QStringLiteral("pattern"), patternName(c->pattern)},
                {QStringLiteral("fromGlued"), c->from.glued()},
                {QStringLiteral("toGlued"), c->to.glued()},
                {QStringLiteral("fromTitle"), title(c->from)},
                {QStringLiteral("toTitle"), title(c->to)},
                {QStringLiteral("groupId"), c->groupId},
                {QStringLiteral("textStyle"), textMap(c->textStyle)},
                {QStringLiteral("data"), dataList(c->data)}};
    }
    return {};
}

void DiagramService::setTool(const QString &tool)
{
    static const QStringList tools{QStringLiteral("pointer"), QStringLiteral("connector"), QStringLiteral("text")};
    if (!tools.contains(tool) || tool == m_tool) {
        return;
    }
    m_tool = tool;
    emit toolChanged();
}

void DiagramService::setFormatPainter(bool armed)
{
    const bool requested = armed;
    if (armed && m_selection.isEmpty()) {
        refuse(tr("Format painter"), tr("Select the shape whose look you want to copy first."));
        armed = false;
    }
    m_formatSource = armed ? m_selection.first() : QString();
    if (armed == m_formatPainter) {
        if (requested != armed) {
            emit formatPainterChanged(); // a refusal: views reset their toggles
        }
        return;
    }
    m_formatPainter = armed;
    emit formatPainterChanged();
}

QString DiagramService::themeId() const
{
    return drawing().themeId;
}

QVariantList DiagramService::themeList() const
{
    QVariantList out;
    for (const Theme &t : themes()) {
        QVariantList swatches;
        for (int i = 0; i < 6 && i < t.roles.size(); ++i) {
            swatches.append(colorName(t.roles.at(i).line));
        }
        out.append(QVariantMap{{QStringLiteral("id"), t.id},
                               {QStringLiteral("name"), t.name},
                               {QStringLiteral("font"), t.font},
                               {QStringLiteral("swatches"), swatches}});
    }
    return out;
}

QVariantList DiagramService::stencilDefinitions() const
{
    // Thumbnails are the painter's own drawing of each stencil in the
    // drawing's theme, as data URLs (no image provider to register).
    if (m_stencilTheme == drawing().themeId && !m_stencilCache.isEmpty()) {
        return m_stencilCache;
    }
    m_stencilTheme = drawing().themeId;
    m_stencilCache.clear();
    for (const Stencil &st : stencils()) {
        if (!st.palette) {
            continue;
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        stencilThumbnail(st.id, m_stencilTheme, 96).save(&buffer, "PNG");
        m_stencilCache.append(QVariantMap{
            {QStringLiteral("id"), st.id},
            {QStringLiteral("label"), st.title},
            {QStringLiteral("category"), categoryTitle(st.category)},
            {QStringLiteral("description"), st.keywords.isEmpty() ? st.title : st.title + QStringLiteral(" — ") + st.keywords},
            {QStringLiteral("imageSource"), QString(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()))}});
    }
    return m_stencilCache;
}

QVariantList DiagramService::paperSizes() const
{
    static const QHash<QString, const char *> labels = {
        {QStringLiteral("letter"), QT_TRANSLATE_NOOP("DiagramService", "Letter (8.5 × 11 in)")},
        {QStringLiteral("legal"), QT_TRANSLATE_NOOP("DiagramService", "Legal (8.5 × 14 in)")},
        {QStringLiteral("tabloid"), QT_TRANSLATE_NOOP("DiagramService", "Tabloid (11 × 17 in)")},
        {QStringLiteral("a5"), QT_TRANSLATE_NOOP("DiagramService", "A5 (148 × 210 mm)")},
        {QStringLiteral("a4"), QT_TRANSLATE_NOOP("DiagramService", "A4 (210 × 297 mm)")},
        {QStringLiteral("a3"), QT_TRANSLATE_NOOP("DiagramService", "A3 (297 × 420 mm)")},
    };
    QVariantList out;
    for (const QString &name : paperNames()) {
        out.append(QVariantMap{{QStringLiteral("id"), name},
                               {QStringLiteral("label"), QCoreApplication::translate("DiagramService", labels.value(name))}});
    }
    return out;
}

QString DiagramService::statusText() const
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    if (m_selection.isEmpty()) {
        return tr("%1 · %n shape(s)", nullptr, int(page->shapes.size())).arg(page->name);
    }
    if (m_selection.size() == 1) {
        if (const Shape *s = page->shape(m_selection.first())) {
            const Stencil *stencil = Diagram::stencil(s->type);
            return tr("%1 · %2 × %3 at %4, %5")
                .arg(stencil != nullptr ? stencil->title : s->type)
                .arg(qRound(s->rect.width()))
                .arg(qRound(s->rect.height()))
                .arg(qRound(s->rect.x()))
                .arg(qRound(s->rect.y()));
        }
        return tr("Connector");
    }
    return tr("%n items selected", nullptr, int(m_selection.size()));
}

void DiagramService::setEditingTextId(const QString &id)
{
    if (id == m_editingTextId) {
        return;
    }
    m_editingTextId = id;
    emit editingTextIdChanged();
}

bool DiagramService::canPaste() const
{
    const QMimeData *mime = QGuiApplication::clipboard() != nullptr ? QGuiApplication::clipboard()->mimeData() : nullptr;
    return mime != nullptr && (mime->hasFormat(QStringLiteral("application/x-qindadiagram-fragment")) || mime->hasImage());
}

} // namespace QindaOffice::Apps::Diagram
