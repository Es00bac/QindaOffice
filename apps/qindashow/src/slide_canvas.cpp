// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_canvas.h"

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

constexpr qreal kGripRadius = 5.0;

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
        const QRectF grip(rect.bottomRight() - QPointF(kGripRadius, kGripRadius),
                          QSizeF(kGripRadius * 2, kGripRadius * 2));
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
}

void SlideCanvas::setSlide(ShowModel::Slide *slide, const QSizeF &slideSize)
{
    commitTextEdit(true); // never drop an in-flight edit
    m_slide = slide;
    m_slideSize = slideSize;
    rebuildItems();
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
            if (shapeItem->shapeId() == shapeId) {
                return shapeItem;
            }
        }
    }
    return nullptr;
}

int SlideCanvas::shapeIndex(const QString &shapeId) const
{
    if (m_slide == nullptr) {
        return -1;
    }
    for (int i = 0; i < m_slide->shapes.size(); ++i) {
        if (m_slide->shapes.at(i).id == shapeId) {
            return i;
        }
    }
    return -1;
}

QStringList SlideCanvas::shapeOrder() const
{
    QStringList ids;
    if (m_slide == nullptr) {
        return ids;
    }
    for (const ShowModel::Shape &shape : m_slide->shapes) {
        ids.append(shape.id);
    }
    return ids;
}

ShapeItem *SlideCanvas::selectedItem() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (auto *shapeItem = qgraphicsitem_cast<ShapeItem *>(item)) {
            return shapeItem;
        }
    }
    return nullptr;
}

ShapeItem *SlideCanvas::addShape(ShowModel::ShapeKind kind, const QRectF &rect)
{
    if (m_slide == nullptr) {
        return nullptr;
    }
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
    ShapeItem *item = selectedItem();
    if (item == nullptr || m_slide == nullptr) {
        return;
    }
    const int index = shapeIndex(item->shapeId());
    const ShowModel::Shape removed = *item->modelShape();
    m_scene->removeItem(item);
    delete item;
    m_slide->shapes.removeAt(index);
    emit shapeRemoved(index, removed);
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

void SlideCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_textItem != nullptr) {
        // Clicks outside the editor frame commit the edit.
        const QPointF scenePos = mapToScene(event->position().toPoint());
        if (!m_textItem->boundingRect().translated(m_textItem->pos()).contains(scenePos)) {
            commitTextEdit(false);
        }
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        // Corner-grip resize on the selected item.
        ShapeItem *item = selectedItem();
        if (item != nullptr) {
            const QPointF scenePos = mapToScene(event->position().toPoint());
            const QRectF grip(item->modelShape()->rect.normalized().bottomRight()
                                  - QPointF(kGripRadius, kGripRadius),
                              QSizeF(kGripRadius * 2, kGripRadius * 2));
            if (grip.contains(scenePos)) {
                m_resizing = item;
                m_resizeOrigRect = item->modelShape()->rect;
                m_resizeStart = scenePos;
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void SlideCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing != nullptr) {
        const QPointF delta = mapToScene(event->position().toPoint()) - m_resizeStart;
        QRectF rect = m_resizeOrigRect;
        rect.setBottomRight(rect.bottomRight() + delta);
        if (rect.width() >= 20 && rect.height() >= 14) {
            m_resizing->modelShape()->rect = rect.normalized();
            m_resizing->update();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void SlideCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing != nullptr && event->button() == Qt::LeftButton) {
        emit shapeGeometryEdited(m_resizing->shapeId(), m_resizeOrigRect,
                                 m_resizing->modelShape()->rect);
        emit slideEdited();
        m_resizing = nullptr;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SlideCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    ShapeItem *item = selectedItem();
    if (item == nullptr) {
        const QList<QGraphicsItem *> atPoint =
            m_scene->items(mapToScene(event->position().toPoint()));
        for (QGraphicsItem *candidate : atPoint) {
            if (auto *shapeItem = qgraphicsitem_cast<ShapeItem *>(candidate)) {
                item = shapeItem;
                break;
            }
        }
    }
    if (item != nullptr && m_slide != nullptr) {
        beginTextEdit(item);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SlideCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_textItem != nullptr) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && selectedItem() != nullptr) {
        deleteSelected();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cancel)) {
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void SlideCanvas::beginTextEdit(ShapeItem *item)
{
    commitTextEdit(true);
    ShowModel::Shape *shape = item->modelShape();
    m_textShapeId = shape->id;
    m_textOriginal = shape->paragraphs;
    auto *document = documentFromParagraphs(shape->paragraphs, this);
    m_textItem = m_scene->addText(QString());
    m_textItem->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_textItem->setTextWidth(qMax<qreal>(shape->rect.width() - 12, 40));
    m_textItem->setDocument(document);
    m_textItem->setPos(shape->rect.normalized().topLeft() + QPointF(6, 4));
    m_textItem->setZValue(1000.0);
    m_scene->clearSelection();
    m_textItem->setSelected(true);
}

void SlideCanvas::commitTextEdit(bool cancelled)
{
    if (m_textItem == nullptr) {
        return;
    }
    const QString shapeId = m_textShapeId;
    const QList<ShowModel::TextParagraph> original = m_textOriginal;
    const QList<ShowModel::TextParagraph> committed = paragraphsFromDocument(*m_textItem->document());
    m_scene->removeItem(m_textItem);
    delete m_textItem;
    m_textItem = nullptr;
    m_textShapeId.clear();
    m_textOriginal.clear();
    if (m_slide == nullptr || cancelled) {
        return;
    }
    const int index = shapeIndex(shapeId);
    if (index < 0) {
        return;
    }
    ShowModel::Shape *shape = &m_slide->shapes[index];
    if (committed != shape->paragraphs) {
        shape->paragraphs = committed;
        emit shapeTextEdited(shapeId, original, committed);
        emit slideEdited();
    }
    if (ShapeItem *item = itemFor(shapeId)) {
        item->update();
    }
}

} // namespace QindaOffice::Apps::Show
