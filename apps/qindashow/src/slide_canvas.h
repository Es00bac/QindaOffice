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
    // The item's bounding rect is read from the model, so the scene must be
    // told BEFORE the model's rect changes underneath it.
    void prepareGeometryChangeForRect() { prepareGeometryChange(); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString m_shapeId;
    SlideCanvas *m_canvas;
};

class SlideCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    // Half-size of the bottom-right resize grip, in scene units. Shared by
    // the painting half and the hit-testing half of the canvas.
    static constexpr qreal kGripRadius = 5.0;

    // View zoom (View > Zoom In/Out/Reset). Presentation state only: the
    // canvas transform scales, the slide model is untouched, so nothing here
    // dirties the document.
    void setZoom(qreal zoom);
    [[nodiscard]] qreal zoom() const { return m_zoom; }

    explicit SlideCanvas(QWidget *parent = nullptr);
    ~SlideCanvas() override;

    void setSlide(ShowModel::Slide *slide, const QSizeF &slideSize);
    // The master the current slide renders with. A COPY, not a pointer: the
    // deck's master list reallocates whenever a master is added or removed.
    void setMaster(const ShowModel::SlideMaster *master);
    [[nodiscard]] const ShowModel::SlideMaster *master() const
    {
        return m_hasMaster ? &m_master : nullptr;
    }
    [[nodiscard]] ShowModel::Slide *slide() const { return m_slide; }
    [[nodiscard]] QSizeF slideSize() const { return m_slideSize; }
    [[nodiscard]] ShapeItem *selectedItem() const;
    // Every selected shape, in deck (z) order. Align and distribute need the
    // whole selection, not just the first item.
    [[nodiscard]] QStringList selectedShapeIds() const;
    // Applies new rects to several shapes in one go (align/distribute/
    // duplicate-offset). Reports only slideEdited(): the caller wraps the
    // whole operation in ONE SlideStateCommand, because lining six shapes up
    // is one action to the user and must be one Ctrl+Z.
    void setShapeRects(const QHash<QString, QRectF> &rects);
    // Z-order to the extremes; raiseSelected()/lowerSelected() move one step.
    void bringSelectedToFront();
    void sendSelectedToBack();
    // Grouping: shapes in a group select, move and resize as one.
    void groupSelected();
    void ungroupSelected();
    [[nodiscard]] bool selectionIsGrouped() const;
    // Snapping to the slide's centre and edges and to other shapes; the
    // guides are drawn while a drag is live.
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const { return m_snap; }
    // Applies snapping to a proposed rect for `shapeId` and records the
    // guides to draw. AGENT-CONTRACT: called from ShapeItem::itemChange, so
    // it must not touch the scene's item list.
    [[nodiscard]] QRectF snapRect(const QString &shapeId, const QRectF &proposed);
    void clearGuides();
    // The shape the pointer has hold of during a drag, or empty. Only that
    // one snaps; the rest of a multi-selection follow it exactly.
    [[nodiscard]] QString draggingShapeId() const { return m_draggingShapeId; }
    [[nodiscard]] ShapeItem *itemFor(const QString &shapeId) const;

    // Model-mutating operations; each emits the matching *_Edited signal
    // with old/new payloads for the window's undo commands.
    ShapeItem *addShape(ShowModel::ShapeKind kind, const QRectF &rect);
    void addImage(const QString &path, const QRectF &rect);
    void deleteSelected();
    void raiseSelected();
    void lowerSelected();
    void toggleBuildSelected();
    // Selects every shape on the slide (Select All).
    void selectAllShapes();
    // M13 styling ops; each emits shapeStyled with the full before/after
    // shape states for the window's ShapeStateCommand.
    void setSelectedFill(const QColor &color);
    void setSelectedStroke(const QColor &color);
    void setSelectedTextBold(bool bold);
    void setSelectedTextItalic(bool italic);
    // Inserts an externally-built shape (clipboard paste), selects it and
    // reports it through the normal shapeInserted undo payload.
    void insertShape(ShowModel::Shape shape);

signals:
    void shapeInserted(const QString &shapeId);
    void shapeRemoved(int index, const ShowModel::Shape &removed);
    void shapeGeometryEdited(const QString &shapeId, const QRectF &oldRect, const QRectF &newRect);
    void shapeTextEdited(const QString &shapeId, const QList<ShowModel::TextParagraph> &oldText,
                         const QList<ShowModel::TextParagraph> &newText);
    void shapeBuildEdited(const QString &shapeId, int oldStep, int newStep);
    void shapesReordered(const QStringList &oldOrder, const QStringList &newOrder);
    void shapeStyled(const QString &shapeId, const ShowModel::Shape &before,
                     const ShowModel::Shape &after);
    void selectionChanged(bool hasSelection);
    void slideEdited(); // any of the above; window marks dirty + journals
    // A gesture that will touch SEVERAL shapes is starting/finishing (a
    // group or multi-selection drag). The window snapshots the slide on
    // the first and pushes ONE undo command on the second, so dragging six
    // shapes is one Ctrl+Z rather than six.
    void compoundEditStarted();
    void compoundEditFinished();

protected:
    // The slide PAGE. AGENT-GUARD: nothing else paints it — the scene holds
    // one item per shape and no item for the slide itself, so before this
    // existed an empty slide rendered as an empty dark-grey view and the
    // editor gave no clue where the slide's edges were.
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // Scale that makes the whole page fit the viewport; the user's zoom
    // multiplies it, so zoom 100% means "the whole slide, as large as it
    // goes" rather than "720 pt at one pixel per point".
    [[nodiscard]] qreal fitScale() const;
    void applyViewTransform();
    // Rewrites the model's shape order and re-applies item z values.
    void reorderTo(const QStringList &order);
    void rebuildItems();
    void applyZOrder();
    int shapeIndex(const QString &shapeId) const;
    QStringList shapeOrder() const;
    void beginTextEdit(ShapeItem *item);
    void commitTextEdit(bool cancelled);

    QGraphicsScene *m_scene;
    ShowModel::Slide *m_slide = nullptr;
    qreal m_zoom = 1.0;
    QSizeF m_slideSize{720.0, 405.0};
    QGraphicsTextItem *m_textItem = nullptr;
    QString m_textShapeId;
    QList<ShowModel::TextParagraph> m_textOriginal;
    // Corner-grip resize drag state.
    ShapeItem *m_resizing = nullptr;
    QRectF m_resizeOrigRect;
    QPointF m_resizeStart;
    ShowModel::SlideMaster m_master;
    bool m_hasMaster = false;
    bool m_snap = true;
    QString m_draggingShapeId;
    // Guides to draw for the live drag, in scene coordinates.
    QList<QLineF> m_guides;
    // True between compoundEditStarted and compoundEditFinished.
    bool m_compound = false;
};

} // namespace QindaOffice::Apps::Show
