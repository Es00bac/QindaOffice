// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_commands.h"

namespace QindaOffice::Apps::Show {

InsertShapeCommand::InsertShapeCommand(ShowModel::Slide *slide, QString shapeId,
                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_shapeId(std::move(shapeId))
{
    setText(QObject::tr("Insert shape"));
    for (int i = 0; i < m_slide->shapes.size(); ++i) {
        if (m_slide->shapes.at(i).id == m_shapeId) {
            m_index = i;
            m_copy = m_slide->shapes.at(i);
            break;
        }
    }
}

void InsertShapeCommand::redo()
{
    if (m_slide->findShape(m_shapeId) == nullptr && m_index >= 0
        && m_index <= m_slide->shapes.size()) {
        ShowModel::Shape shape = m_copy;
        shape.id = m_shapeId;
        m_slide->shapes.insert(m_index, shape);
    }
}

void InsertShapeCommand::undo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        m_copy = *shape;
        m_index = m_slide->shapes.indexOf(*shape);
        m_slide->shapes.removeAll(*shape);
    }
}

RemoveShapeCommand::RemoveShapeCommand(ShowModel::Slide *slide, int index, ShowModel::Shape removed,
                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_index(index)
    , m_removed(std::move(removed))
{
    setText(QObject::tr("Delete shape"));
}

void RemoveShapeCommand::redo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_removed.id)) {
        m_slide->shapes.removeAll(*shape);
    }
}

void RemoveShapeCommand::undo()
{
    m_slide->shapes.insert(qMin(m_index, m_slide->shapes.size()), m_removed);
}

GeometryCommand::GeometryCommand(ShowModel::Slide *slide, QString shapeId, QRectF oldRect,
                                 QRectF newRect, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_shapeId(std::move(shapeId))
    , m_oldRect(oldRect)
    , m_newRect(newRect)
{
    setText(QObject::tr("Move/resize shape"));
}

void GeometryCommand::redo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->rect = m_newRect;
    }
}

void GeometryCommand::undo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->rect = m_oldRect;
    }
}

bool GeometryCommand::mergeWith(const QUndoCommand *other)
{
    const auto *that = static_cast<const GeometryCommand *>(other);
    if (that->m_slide != m_slide || that->m_shapeId != m_shapeId) {
        return false;
    }
    m_newRect = that->m_newRect;
    return true;
}

TextEditCommand::TextEditCommand(ShowModel::Slide *slide, QString shapeId,
                                 QList<ShowModel::TextParagraph> oldText,
                                 QList<ShowModel::TextParagraph> newText, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_shapeId(std::move(shapeId))
    , m_oldText(std::move(oldText))
    , m_newText(std::move(newText))
{
    setText(QObject::tr("Edit text"));
}

void TextEditCommand::redo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->paragraphs = m_newText;
    }
}

void TextEditCommand::undo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->paragraphs = m_oldText;
    }
}

BuildStepCommand::BuildStepCommand(ShowModel::Slide *slide, QString shapeId, int oldStep,
                                   int newStep, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_shapeId(std::move(shapeId))
    , m_oldStep(oldStep)
    , m_newStep(newStep)
{
    setText(QObject::tr("Toggle build step"));
}

void BuildStepCommand::redo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->buildStep = m_newStep;
    }
}

void BuildStepCommand::undo()
{
    if (ShowModel::Shape *shape = m_slide->findShape(m_shapeId)) {
        shape->buildStep = m_oldStep;
    }
}

ShapeOrderCommand::ShapeOrderCommand(ShowModel::Slide *slide, QStringList oldOrder,
                                     QStringList newOrder, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_slide(slide)
    , m_oldOrder(std::move(oldOrder))
    , m_newOrder(std::move(newOrder))
{
    setText(QObject::tr("Change z-order"));
}

void ShapeOrderCommand::redo()
{
    apply(m_newOrder);
}

void ShapeOrderCommand::undo()
{
    apply(m_oldOrder);
}

void ShapeOrderCommand::apply(const QStringList &order)
{
    QList<ShowModel::Shape> reordered;
    for (const QString &id : order) {
        if (ShowModel::Shape *shape = m_slide->findShape(id)) {
            reordered.append(*shape);
        }
    }
    for (const ShowModel::Shape &shape : m_slide->shapes) {
        if (!order.contains(shape.id)) {
            reordered.append(shape);
        }
    }
    m_slide->shapes = reordered;
}

SlideListCommand::SlideListCommand(ShowModel::Presentation *deck, QList<ShowModel::Slide> before,
                                   QList<ShowModel::Slide> after, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_deck(deck)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
    setText(QObject::tr("Change slides"));
}

void SlideListCommand::redo()
{
    m_deck->slides = m_after;
}

void SlideListCommand::undo()
{
    m_deck->slides = m_before;
}

} // namespace QindaOffice::Apps::Show
