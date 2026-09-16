// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_canvas.h"

#include <algorithm>
#include <functional>

#include "slide_renderer.h"
#include "text_bridge.h"

#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QUuid>

namespace QindaOffice::Apps::Show {

namespace {

QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QRectF defaultRectFor(ShowModel::ShapeKind kind, const QSizeF &slide)
{
    switch (kind) {
    case ShowModel::ShapeKind::TextBox:
        return QRectF(slide.width() * 0.2, slide.height() * 0.4, slide.width() * 0.6,
                      slide.height() * 0.2);
    case ShowModel::ShapeKind::Image:
        return QRectF(slide.width() * 0.3, slide.height() * 0.2, slide.width() * 0.4,
                      slide.height() * 0.5);
    default:
        return QRectF(slide.width() * 0.35, slide.height() * 0.35, slide.width() * 0.3,
                      slide.height() * 0.3);
    }
}


} // namespace

// --- ShapeItem ------------------------------------------------------------

ShapeItem::ShapeItem(QString shapeId, SlideCanvas *canvas)
    : m_shapeId(std::move(shapeId))
    , m_canvas(canvas)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
}

ShowModel::Shape *ShapeItem::modelShape() const
{
    ShowModel::Slide *slide = m_canvas->slide();
    return slide != nullptr ? slide->findShape(m_shapeId) : nullptr;
}

QRectF ShapeItem::boundingRect() const
{
    // Grow slightly so a hairline stroke and the selection frame are
    // never clipped.
    const ShowModel::Shape *shape = modelShape();
    return shape != nullptr ? shape->rect.normalized().adjusted(-3, -3, 3, 3) : QRectF();
}

void ShapeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const ShowModel::Shape *shape = modelShape();
    if (shape == nullptr) {
        return;
    }
    ShowModel::renderShape(painter, *shape);
    if (isSelected()) {
        painter->save();
        QPen pen(QColor(61, 123, 204));
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QRectF rect = shape->rect.normalized();
        painter->drawRect(rect);
        const QRectF grip(rect.bottomRight()
                              - QPointF(SlideCanvas::kGripRadius, SlideCanvas::kGripRadius),
                          QSizeF(SlideCanvas::kGripRadius * 2, SlideCanvas::kGripRadius * 2));
        painter->fillRect(grip, QColor(61, 123, 204));
        painter->restore();
    }
}

QVariant ShapeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    // The item moves in scene coordinates, which are model coordinates
    // (the item has no position of its own), so a position delta is a
    // rect translation. The model write-back happens here; the undo
    // signal carries old/new rects for the window's command.
    if (change == ItemPositionHasChanged && !pos().isNull()) {
        if (ShowModel::Shape *shape = modelShape()) {
            const QRectF oldRect = shape->rect;
            shape->rect.translate(pos());
            setPos(QPointF());
            emit m_canvas->shapeGeometryEdited(shape->id, oldRect, shape->rect);
            emit m_canvas->slideEdited();
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

// --- SlideCanvas ----------------------------------------------------------

SlideCanvas::SlideCanvas(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);
    setAlignment(Qt::AlignCenter);
    setBackgroundBrush(QColor(70, 70, 70));
    setFrameShape(QFrame::NoFrame);
    connect(m_scene, &QGraphicsScene::selectionChanged, this,
            [this] { emit selectionChanged(selectedItem() != nullptr); });
}

void SlideCanvas::setSlide(ShowModel::Slide *slide, const QSizeF &slideSize)
{
    // AGENT-GUARD: COMMIT (cancelled=false) — the `true` argument here was
    // the cancel path, so switching slides silently discarded a typed edit
    // while the comment claimed the opposite.
    commitTextEdit(false);
    // Selection survives rebuilds (setSlide re-runs after every undo via
    // the window's fullRefresh): remember shape ids, not item pointers.
    QStringList selectedIds;
    for (const QGraphicsItem *item : m_scene->selectedItems()) {
        if (const auto *shapeItem = qgraphicsitem_cast<const ShapeItem *>(item)) {
            selectedIds.append(shapeItem->shapeId());
        }
    }
    m_slide = slide;
    m_slideSize = slideSize;
    rebuildItems();
    for (const QString &id : selectedIds) {
        if (ShapeItem *item = itemFor(id)) item->setSelected(true);
    }
}

SlideCanvas::~SlideCanvas()
{
    // QObject teardown order is not ours: the window's actions may be
    // gone while the scene still destroys items and re-fires selection
    // changes. Never emit signals from the destructor path (a connected
    // window lambda would touch dead QObjects).
    disconnect(m_scene, nullptr, this, nullptr);
}

void SlideCanvas::rebuildItems()
{
    m_scene->clear();
    m_textItem = nullptr;
    m_resizing = nullptr;
    if (m_slide == nullptr) {
        return;
    }
    m_scene->setSceneRect(QRectF(QPointF(0, 0), m_slideSize));
    for (ShowModel::Shape &shape : m_slide->shapes) {
        m_scene->addItem(new ShapeItem(shape.id, this));
    }
    applyZOrder();
}

void SlideCanvas::applyZOrder()
{
    // Item z follows the model list order (later = in front).
    const QStringList order = shapeOrder();
    for (int i = 0; i < order.size(); ++i) {
        if (ShapeItem *item = itemFor(order.at(i))) {
            item->setZValue(1.0 + i);
        }
    }
}

ShapeItem *SlideCanvas::itemFor(const QString &shapeId) const
{
    const QList<QGraphicsItem *> all = m_scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : all) {
        if (auto *shapeItem = qgraphicsitem_cast<ShapeItem *>(item)) {
            if (shapeItem->shapeId() == shapeId) return shapeItem;
        }
    }
    return nullptr;
}

int SlideCanvas::shapeIndex(const QString &shapeId) const
{
    if (m_slide == nullptr) return -1;
    for (int i = 0; i < m_slide->shapes.size(); ++i) {
        if (m_slide->shapes.at(i).id == shapeId) return i;
    }
    return -1;
}

QStringList SlideCanvas::shapeOrder() const
{
    QStringList ids;
    if (m_slide == nullptr) return ids;
    for (const ShowModel::Shape &shape : m_slide->shapes) {
        ids.append(shape.id);
    }
    return ids;
}

namespace {

// Styling helper: snapshot the selected shape, apply `mutate`, emit the
// before/after payload; false when nothing is selected or unchanged.
bool restyleSelection(SlideCanvas *canvas,
                      const std::function<void(ShowModel::Shape &)> &mutate)
{
    ShapeItem *item = canvas->selectedItem();
    if (item == nullptr || item->modelShape() == nullptr) return false;
    ShowModel::Shape *shape = item->modelShape();
    const ShowModel::Shape before = *shape;
    mutate(*shape);
    if (*shape == before) return false;
    emit canvas->shapeStyled(shape->id, before, *shape);
    emit canvas->slideEdited();
    return true;
}

} // namespace

void SlideCanvas::setSelectedFill(const QColor &color)
{
    restyleSelection(this, [color](ShowModel::Shape &shape) { shape.fill = color; });
}

void SlideCanvas::setSelectedStroke(const QColor &color)
{
    restyleSelection(this, [color](ShowModel::Shape &shape) { shape.stroke = color; });
}

void SlideCanvas::setSelectedTextBold(bool bold)
{
    restyleSelection(this, [bold](ShowModel::Shape &shape) {
        for (auto &paragraph : shape.paragraphs)
            for (auto &run : paragraph.runs) run.bold = bold;
    });
}

void SlideCanvas::setSelectedTextItalic(bool italic)
{
    restyleSelection(this, [italic](ShowModel::Shape &shape) {
        for (auto &paragraph : shape.paragraphs)
            for (auto &run : paragraph.runs) run.italic = italic;
    });
}

void SlideCanvas::selectAllShapes()
{
    const auto items = m_scene->items();
    for (QGraphicsItem *item : items) item->setSelected(true);
}

void SlideCanvas::insertShape(ShowModel::Shape shape)
{
    if (m_slide == nullptr) return;
    const QString id = shape.id;
    m_slide->shapes.append(shape);
    rebuildItems();
    m_scene->clearSelection();
    if (ShapeItem *item = itemFor(id)) {
        item->setSelected(true);
    }
    emit shapeInserted(id);
    emit slideEdited();
}

ShapeItem *SlideCanvas::selectedItem() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (auto *shapeItem = qgraphicsitem_cast<ShapeItem *>(item)) return shapeItem;
    }
    return nullptr;
}

ShapeItem *SlideCanvas::addShape(ShowModel::ShapeKind kind, const QRectF &rect)
{
    if (m_slide == nullptr) return nullptr;
    ShowModel::Shape shape;
    shape.id = freshId();
    shape.kind = kind;
    shape.rect = rect.isValid() ? rect : defaultRectFor(kind, m_slideSize);
    if (kind == ShowModel::ShapeKind::TextBox) {
        shape.strokeWidth = 0; // invisible box until it earns a fill
    } else if (kind != ShowModel::ShapeKind::Line && kind != ShowModel::ShapeKind::Arrow) {
        shape.fill = QColor(91, 155, 213);
    }
    m_slide->shapes.append(shape);
    auto *item = new ShapeItem(shape.id, this);
    m_scene->addItem(item);
    applyZOrder();
    m_scene->clearSelection();
    item->setSelected(true);
    emit shapeInserted(shape.id);
    emit slideEdited();
    return item;
}

void SlideCanvas::addImage(const QString &path, const QRectF &rect)
{
    if (m_slide == nullptr) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    ShowModel::Shape shape;
    shape.id = freshId();
    shape.kind = ShowModel::ShapeKind::Image;
    shape.rect = rect.isValid() ? rect : defaultRectFor(ShowModel::ShapeKind::Image, m_slideSize);
    shape.imagePng = file.readAll();
    shape.strokeWidth = 0;
    m_slide->shapes.append(shape);
    auto *item = new ShapeItem(shape.id, this);
    m_scene->addItem(item);
    applyZOrder();
    m_scene->clearSelection();
    item->setSelected(true);
    emit shapeInserted(shape.id);
    emit slideEdited();
}

void SlideCanvas::deleteSelected()
{
    if (m_slide == nullptr) {
        return;
    }
    // AGENT-GUARD: remove EVERY selected shape (highest model index first so
    // the removals stay index-stable). The single-item version needed one
    // keypress per shape in a rubber-band selection.
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    QVector<QPair<int, ShowModel::Shape>> removed;
    for (QGraphicsItem *graphicsItem : selected) {
        auto *item = qgraphicsitem_cast<ShapeItem *>(graphicsItem);
        if (item == nullptr) {
            continue;
        }
        removed.append({shapeIndex(item->shapeId()), *item->modelShape()});
        m_scene->removeItem(item);
        delete item;
    }
    if (removed.isEmpty()) {
        return;
    }
    std::sort(removed.begin(), removed.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &[index, shape] : removed) {
        if (index < 0 || index >= m_slide->shapes.size()) {
            continue;
        }
        m_slide->shapes.removeAt(index);
        emit shapeRemoved(index, shape);
    }
    emit slideEdited();
}

void SlideCanvas::raiseSelected()
{
    ShapeItem *item = selectedItem();
    if (item == nullptr || m_slide == nullptr) {
        return;
    }
    const int index = shapeIndex(item->shapeId());
    if (index < 0 || index >= m_slide->shapes.size() - 1) {
        return;
    }
    const QStringList before = shapeOrder();
    m_slide->shapes.move(index, index + 1);
    applyZOrder();
    const QStringList after = shapeOrder();
    emit shapesReordered(before, after);
    emit slideEdited();
}

void SlideCanvas::lowerSelected()
{
    ShapeItem *item = selectedItem();
    if (item == nullptr || m_slide == nullptr) {
        return;
    }
    const int index = shapeIndex(item->shapeId());
    if (index <= 0) {
        return;
    }
    const QStringList before = shapeOrder();
    m_slide->shapes.move(index, index - 1);
    applyZOrder();
    const QStringList after = shapeOrder();
    emit shapesReordered(before, after);
    emit slideEdited();
}

void SlideCanvas::toggleBuildSelected()
{
    ShapeItem *item = selectedItem();
    if (item == nullptr) {
        return;
    }
    const int oldStep = item->modelShape()->buildStep;
    const int newStep = oldStep > 0 ? 0 : 1;
    item->modelShape()->buildStep = newStep;
    emit shapeBuildEdited(item->shapeId(), oldStep, newStep);
    emit slideEdited();
}








void SlideCanvas::setZoom(qreal zoom)
{
    // Bounded like every other zoom in the suite; the transform replaces
    // rather than accumulates so Reset is exact.
    const qreal next = qBound(0.25, zoom, 4.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    setTransform(QTransform::fromScale(m_zoom, m_zoom));
}

} // namespace QindaOffice::Apps::Show
