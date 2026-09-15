// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow slide canvas (plan section 6.3): a QGraphicsView over the
// live show_model slide. One ShapeItem per model shape paints through
// show_model::renderShape so the editor is pixel-identical to the player.
// The canvas mutates the model directly and reports every mutation with
// old/new payloads; the window turns them into undo commands, so all
// editing flows through one model.
//
// Interactions: click select, drag move, corner-grip resize, double-click
// edits text in place, Delete removes, Ctrl+]/Ctrl+[ changes z-order.

#include "presentation.h"

#include <QGraphicsItem>
#include <QGraphicsView>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QGraphicsScene;
class QGraphicsTextItem;
class QTextDocument;

namespace QindaOffice::Apps::Show {

class SlideCanvas;

// Scene item bound to a model shape by id. The model list reallocates on
// every append, so items never cache a Shape pointer: modelShape() looks
// the shape up in the live slide on each use. Item position stays (0,0);
// the shape rect lives in model coordinates so paint() can delegate to
// the renderer verbatim.
class ShapeItem final : public QGraphicsItem {
public:
    ShapeItem(QString shapeId, SlideCanvas *canvas);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    [[nodiscard]] ShowModel::Shape *modelShape() const;
    [[nodiscard]] QString shapeId() const { return m_shapeId; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString m_shapeId;
    SlideCanvas *m_canvas;
};

class SlideCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    explicit SlideCanvas(QWidget *parent = nullptr);

    void setSlide(ShowModel::Slide *slide, const QSizeF &slideSize);
    [[nodiscard]] ShowModel::Slide *slide() const { return m_slide; }
    [[nodiscard]] QSizeF slideSize() const { return m_slideSize; }
    [[nodiscard]] ShapeItem *selectedItem() const;
    [[nodiscard]] ShapeItem *itemFor(const QString &shapeId) const;

    // Model-mutating operations; each emits the matching *_Edited signal
    // with old/new payloads for the window's undo commands.
    ShapeItem *addShape(ShowModel::ShapeKind kind, const QRectF &rect);
    void addImage(const QString &path, const QRectF &rect);
    void deleteSelected();
    void raiseSelected();
    void lowerSelected();
    void toggleBuildSelected();

signals:
    void shapeInserted(const QString &shapeId);
    void shapeRemoved(int index, const ShowModel::Shape &removed);
    void shapeGeometryEdited(const QString &shapeId, const QRectF &oldRect, const QRectF &newRect);
    void shapeTextEdited(const QString &shapeId, const QList<ShowModel::TextParagraph> &oldText,
                         const QList<ShowModel::TextParagraph> &newText);
    void shapeBuildEdited(const QString &shapeId, int oldStep, int newStep);
    void shapesReordered(const QStringList &oldOrder, const QStringList &newOrder);
    void slideEdited(); // any of the above; window marks dirty + journals

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuildItems();
    void applyZOrder();
    int shapeIndex(const QString &shapeId) const;
    QStringList shapeOrder() const;
    void beginTextEdit(ShapeItem *item);
    void commitTextEdit(bool cancelled);

    QGraphicsScene *m_scene;
    ShowModel::Slide *m_slide = nullptr;
    QSizeF m_slideSize{720.0, 405.0};
    QGraphicsTextItem *m_textItem = nullptr;
    QString m_textShapeId;
    QList<ShowModel::TextParagraph> m_textOriginal;
    // Corner-grip resize drag state.
    ShapeItem *m_resizing = nullptr;
    QRectF m_resizeOrigRect;
    QPointF m_resizeStart;
};

} // namespace QindaOffice::Apps::Show
