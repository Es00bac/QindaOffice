// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_canvas_item.h"

#include "slide_renderer.h"
#include "text_bridge.h"

#include <QFile>
#include <QPainter>
#include <QTextDocument>
#include <QUuid>

#include <functional>

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
        return QRectF(slide.width() * 0.2, slide.height() * 0.4, slide.width() * 0.6, slide.height() * 0.2);
    case ShowModel::ShapeKind::Image:
        return QRectF(slide.width() * 0.3, slide.height() * 0.2, slide.width() * 0.4, slide.height() * 0.5);
    default:
        return QRectF(slide.width() * 0.35, slide.height() * 0.35, slide.width() * 0.3, slide.height() * 0.3);
    }
}

// Editor chrome colours: the selection outline and the snap guides are
// canvas chrome, not slide content, and stay readable on any master.
const QColor kSelection(61, 123, 204);
const QColor kGuide(0xe0, 0x4f, 0x9a);

} // namespace

SlideCanvasItem::SlideCanvasItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setImplicitWidth(m_slideSize.width());
    setImplicitHeight(m_slideSize.height());
}

SlideCanvasItem::~SlideCanvasItem() = default;

void SlideCanvasItem::setMaster(const ShowModel::SlideMaster *master)
{
    setBackdrop(ShowModel::backdropFor(master), master);
}

void SlideCanvasItem::setBackdrop(const ShowModel::SlideBackdrop &backdrop, const ShowModel::SlideMaster *master)
{
    m_hasMaster = master != nullptr;
    m_master = master != nullptr ? *master : ShowModel::SlideMaster{};
    m_backdrop = backdrop;
    update();
}

void SlideCanvasItem::setSlide(ShowModel::Slide *slide, const QSizeF &slideSize)
{
    // COMMIT a typed edit when the slide changes underneath it.
    commitTextEdit(false);
    m_slide = slide;
    m_slideSize = slideSize;
    setImplicitWidth(slideSize.width());
    setImplicitHeight(slideSize.height());
    // Selection survives a rebuild (setSlide re-runs after every undo via
    // the host's fullRefresh) as long as the ids still exist.
    QSet<QString> kept;
    if (slide != nullptr) {
        for (const ShowModel::Shape &shape : slide->shapes) {
            if (m_selected.contains(shape.id)) {
                kept.insert(shape.id);
            }
        }
    }
    m_selected = kept;
    m_gesture = Gesture::None;
    m_guides.clear();
    notifySelection();
    update();
}

// ---- selection ----------------------------------------------------------------------

QStringList SlideCanvasItem::shapeOrder() const
{
    QStringList ids;
    if (m_slide != nullptr) {
        for (const ShowModel::Shape &shape : m_slide->shapes) {
            ids.append(shape.id);
        }
    }
    return ids;
}

int SlideCanvasItem::shapeIndex(const QString &shapeId) const
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

QStringList SlideCanvasItem::selectedShapeIds() const
{
    QStringList ids;
    for (const QString &id : shapeOrder()) {
        if (m_selected.contains(id)) {
            ids.append(id);
        }
    }
    return ids;
}

QString SlideCanvasItem::selectedShapeId() const
{
    const QStringList ids = selectedShapeIds();
    return ids.isEmpty() ? QString() : ids.first();
}

const ShowModel::Shape *SlideCanvasItem::selectedShape() const
{
    const QString id = selectedShapeId();
    return m_slide != nullptr && !id.isEmpty() ? m_slide->findShape(id) : nullptr;
}

void SlideCanvasItem::notifySelection()
{
    emit selectionChanged();
    update();
}

void SlideCanvasItem::setSelected(const QString &shapeId, bool selected)
{
    if (m_slide == nullptr || m_slide->findShape(shapeId) == nullptr) {
        return;
    }
    if (selected) {
        m_selected.insert(shapeId);
    } else {
        m_selected.remove(shapeId);
    }
    notifySelection();
}

void SlideCanvasItem::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    notifySelection();
}

void SlideCanvasItem::selectAllShapes()
{
    // AGENT-GUARD: one temporary, held. begin() and end() on two separate
    // shapeOrder() calls are iterators into two different dead lists —
    // undefined behaviour that reached std::bad_alloc (found by test).
    const QStringList order = shapeOrder();
    m_selected = QSet<QString>(order.begin(), order.end());
    notifySelection();
}

QString SlideCanvasItem::shapeAt(const QPointF &slidePos) const
{
    if (m_slide == nullptr) {
        return {};
    }
    // Topmost first: the model list is z order (later = in front).
    for (int i = m_slide->shapes.size() - 1; i >= 0; --i) {
        const ShowModel::Shape &shape = m_slide->shapes.at(i);
        if (shape.rect.normalized().adjusted(-3, -3, 3, 3).contains(slidePos)) {
            return shape.id;
        }
    }
    return {};
}

// ---- shape operations -----------------------------------------------------------------

QString SlideCanvasItem::addShape(ShowModel::ShapeKind kind, const QRectF &rect)
{
    if (m_slide == nullptr) {
        return {};
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
    insertShape(shape);
    return shape.id;
}

QString SlideCanvasItem::addImage(const QString &path, const QRectF &rect)
{
    QFile file(path);
    if (m_slide == nullptr || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    ShowModel::Shape shape;
    shape.id = freshId();
    shape.kind = ShowModel::ShapeKind::Image;
    shape.rect = rect.isValid() ? rect : defaultRectFor(ShowModel::ShapeKind::Image, m_slideSize);
    shape.imagePng = file.readAll();
    shape.strokeWidth = 0;
    insertShape(shape);
    return shape.id;
}

void SlideCanvasItem::insertShape(ShowModel::Shape shape)
{
    if (m_slide == nullptr) {
        return;
    }
    const QString id = shape.id;
    m_slide->shapes.append(std::move(shape));
    m_selected = {id};
    notifySelection();
    emit shapeInserted(id);
    emit slideEdited();
}

void SlideCanvasItem::deleteSelected()
{
    if (m_slide == nullptr || m_selected.isEmpty()) {
        return;
    }
    // Remove EVERY selected shape, highest model index first so the
    // removals stay index-stable.
    QList<QPair<int, ShowModel::Shape>> removed;
    for (const QString &id : selectedShapeIds()) {
        const int index = shapeIndex(id);
        if (index >= 0) {
            removed.append({index, m_slide->shapes.at(index)});
        }
    }
    std::sort(removed.begin(), removed.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    m_selected.clear();
    for (const auto &[index, shape] : removed) {
        m_slide->shapes.removeAt(index);
        emit shapeRemoved(index, shape);
    }
    notifySelection();
    emit slideEdited();
}

void SlideCanvasItem::raiseSelected()
{
    const int index = shapeIndex(selectedShapeId());
    if (m_slide == nullptr || index < 0 || index >= m_slide->shapes.size() - 1) {
        return;
    }
    const QStringList before = shapeOrder();
    m_slide->shapes.move(index, index + 1);
    emit shapesReordered(before, shapeOrder());
    emit slideEdited();
    update();
}

void SlideCanvasItem::lowerSelected()
{
    const int index = shapeIndex(selectedShapeId());
    if (m_slide == nullptr || index <= 0) {
        return;
    }
    const QStringList before = shapeOrder();
    m_slide->shapes.move(index, index - 1);
    emit shapesReordered(before, shapeOrder());
    emit slideEdited();
    update();
}

void SlideCanvasItem::toggleBuildSelected()
{
    ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(selectedShapeId()) : nullptr;
    if (shape == nullptr) {
        return;
    }
    const int oldStep = shape->buildStep;
    shape->buildStep = oldStep > 0 ? 0 : 1;
    emit shapeBuildEdited(shape->id, oldStep, shape->buildStep);
    emit slideEdited();
}

namespace {
// Styling helper: snapshot the selected shape, apply `mutate`, emit the
// before/after payload; false when nothing is selected or unchanged.
bool restyleSelection(SlideCanvasItem *canvas, ShowModel::Slide *slide,
                      const std::function<void(ShowModel::Shape &)> &mutate)
{
    ShowModel::Shape *shape = slide != nullptr ? slide->findShape(canvas->selectedShapeId()) : nullptr;
    if (shape == nullptr) {
        return false;
    }
    const ShowModel::Shape before = *shape;
    mutate(*shape);
    if (*shape == before) {
        return false;
    }
    emit canvas->shapeStyled(shape->id, before, *shape);
    emit canvas->slideEdited();
    canvas->update();
    return true;
}
} // namespace

void SlideCanvasItem::setSelectedFill(const QColor &color)
{
    // A solid colour replaces a gradient / picture / pattern fill too.
    restyleSelection(this, m_slide, [color](ShowModel::Shape &shape) {
        shape.fill = color;
        shape.fillStyle = ShowModel::FillStyle();
    });
}

void SlideCanvasItem::setSelectedStroke(const QColor &color)
{
    restyleSelection(this, m_slide, [color](ShowModel::Shape &shape) { shape.stroke = color; });
}

void SlideCanvasItem::setSelectedTextBold(bool bold)
{
    restyleSelection(this, m_slide, [bold](ShowModel::Shape &shape) {
        for (auto &paragraph : shape.paragraphs)
            for (auto &run : paragraph.runs) run.bold = bold;
    });
}

void SlideCanvasItem::setSelectedTextItalic(bool italic)
{
    restyleSelection(this, m_slide, [italic](ShowModel::Shape &shape) {
        for (auto &paragraph : shape.paragraphs)
            for (auto &run : paragraph.runs) run.italic = italic;
    });
}

// ---- painting -------------------------------------------------------------------------

void SlideCanvasItem::paint(QPainter *painter)
{
    ShowModel::paintBackdrop(painter, m_backdrop, m_slideSize);
    if (m_slide == nullptr) {
        return;
    }
    const ShowModel::SlideMaster *typeMaster = m_backdrop.hasTypeMaster ? &m_backdrop.typeMaster : nullptr;
    for (const ShowModel::Shape &shape : m_slide->shapes) {
        // The shape being edited is drawn by the TextEdit over it.
        if (shape.id == m_textShapeId && shape.kind == ShowModel::ShapeKind::TextBox) {
            continue;
        }
        ShowModel::renderShape(painter, shape, typeMaster);
    }
    painter->save();
    QPen pen(kSelection);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (const QString &id : m_selected) {
        const ShowModel::Shape *shape = m_slide->findShape(id);
        if (shape == nullptr) {
            continue;
        }
        const QRectF rect = shape->rect.normalized();
        painter->drawRect(rect);
        if (m_selected.size() == 1) {
            const QRectF grip(rect.bottomRight() - QPointF(kGripRadius, kGripRadius),
                              QSizeF(kGripRadius * 2, kGripRadius * 2));
            painter->fillRect(grip, kSelection);
        }
    }
    if (m_gesture == Gesture::Marquee && m_marquee.isValid()) {
        QPen dashed(kSelection);
        dashed.setCosmetic(true);
        dashed.setStyle(Qt::DashLine);
        painter->setPen(dashed);
        painter->drawRect(m_marquee);
    }
    if (!m_guides.isEmpty()) {
        QPen guide(kGuide);
        guide.setCosmetic(true);
        guide.setStyle(Qt::DashLine);
        painter->setPen(guide);
        for (const QLineF &line : m_guides) {
            painter->drawLine(line);
        }
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Show
