// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow slide canvas as a QtQuick item (conversion plan §5.3): a
// QQuickPaintedItem the size of the slide, in slide points, painting the
// page (the master's background), every shape through show_model's
// renderShape — the same painter the thumbnails, the player and the PDF
// use — the selection outline with its resize grip, the marquee and the
// snap guides. A Tk.Viewport around it zooms and scrolls (D-009).
//
// The canvas mutates the model directly and reports every mutation with
// old/new payloads (D-007); the host turns them into undo commands, so all
// editing flows through one model. Selection is a set of shape ids (the
// model's shape list reallocates on every append, so nothing here holds a
// Shape pointer across a call). In-place text editing: beginTextEdit()
// builds a QTextDocument from the shape's runs (text_bridge.h) and hands
// it to the QML TextEdit the window places at editorRect; commitTextEdit()
// reads the runs back.
//
// Tables (M67, D-076): a click into the selected table puts the caret in
// the cell under it (the same in-place editor, over the cell's box), a
// drag selects cells, Tab / Shift+Tab move between cells, dragging a
// column or row boundary resizes; table and chart edits arrive as one
// whole-shape before/after (shapeEdited), which the host pushes as an
// id-located ShapeEditCommand.
//
// Files: slide_canvas_item.cpp (state, painting, shape ops),
// slide_canvas_item_input.cpp (pointer and key events, the text editor),
// slide_canvas_item_snap.cpp (snapping, guides, grouping),
// slide_canvas_item_order.cpp (multi-selection, z-order),
// slide_canvas_item_table.cpp (cells, boundaries, cell editing; M67),
// slide_canvas_item_marks.cpp (placeholder frames, comment and media
// marks; M75; empty placeholders' prompts, M82).

#include "presentation.h"
#include "slide_renderer.h"
#include "table_edit.h"

#include <QQuickPaintedItem>
#include <QSet>
#include <QtQml/qqmlregistration.h>

#include <functional>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QTextDocument;

namespace QindaOffice::Apps::Show {

class SlideCanvasItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString editingShapeId READ editingShapeId NOTIFY editingChanged)
    Q_PROPERTY(QRectF editorRect READ editorRect NOTIFY editingChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapChanged)
    // True while the in-place editor holds a table cell (Tab moves cells).
    Q_PROPERTY(bool editingCell READ editingCell NOTIFY editingChanged)
public:
    // Half-size of the bottom-right resize grip, in slide points.
    static constexpr qreal kGripRadius = 5.0;

    explicit SlideCanvasItem(QQuickItem *parent = nullptr);
    ~SlideCanvasItem() override;

    void setSlide(ShowModel::Slide *slide, const QSizeF &slideSize);
    // The master the current slide renders with. A COPY, not a pointer: the
    // deck's master list reallocates whenever a master is added or removed.
    void setMaster(const ShowModel::SlideMaster *master);
    // M62: what the slide paints over — background, master and layout
    // decorations, a QindaShow master's type defaults (slide_renderer.h) —
    // and the master itself (master() keeps answering for the dialogs).
    void setBackdrop(const ShowModel::SlideBackdrop &backdrop, const ShowModel::SlideMaster *master);
    [[nodiscard]] const ShowModel::SlideMaster *master() const { return m_hasMaster ? &m_master : nullptr; }
    [[nodiscard]] ShowModel::Slide *slide() const { return m_slide; }
    [[nodiscard]] QSizeF slideSize() const { return m_slideSize; }

    // ---- selection (ids; z order when listed) ----
    [[nodiscard]] bool hasSelection() const { return !m_selected.isEmpty(); }
    [[nodiscard]] int selectionCount() const { return int(m_selected.size()); }
    [[nodiscard]] QString selectedShapeId() const;
    [[nodiscard]] QStringList selectedShapeIds() const;
    [[nodiscard]] const ShowModel::Shape *selectedShape() const;
    [[nodiscard]] bool isSelected(const QString &shapeId) const { return m_selected.contains(shapeId); }
    Q_INVOKABLE void setSelected(const QString &shapeId, bool selected);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAllShapes();
    [[nodiscard]] QString shapeAt(const QPointF &slidePos) const;

    // ---- model-mutating operations; each emits its *_Edited signal ----
    QString addShape(ShowModel::ShapeKind kind, const QRectF &rect);
    Q_INVOKABLE QString addShapeKind(int kind, const QRectF &rect) { return addShape(ShowModel::ShapeKind(kind), rect); }
    QString addImage(const QString &path, const QRectF &rect);
    void insertShape(ShowModel::Shape shape);
    Q_INVOKABLE void deleteSelected();
    void raiseSelected();
    void lowerSelected();
    void bringSelectedToFront();
    void sendSelectedToBack();
    void toggleBuildSelected();
    void setSelectedFill(const QColor &color);
    void setSelectedStroke(const QColor &color);
    void setSelectedTextBold(bool bold);
    void setSelectedTextItalic(bool italic);
    // Applies new rects to several shapes in one go (align/distribute);
    // reports only slideEdited() — the caller wraps it in ONE command.
    void setShapeRects(const QHash<QString, QRectF> &rects);
    void groupSelected();
    void ungroupSelected();
    [[nodiscard]] bool selectionIsGrouped() const;
    [[nodiscard]] bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool enabled);
    [[nodiscard]] QRectF snapRect(const QString &shapeId, const QRectF &proposed);
    void clearGuides();

    // ---- tables (slide_canvas_item_table.cpp) ----
    // The table the table commands act on: the one selected shape when it
    // is a table, else "".
    [[nodiscard]] QString tableShapeId() const;
    // The cells selected in it — the edited cell's block while the caret is
    // in one; invalid when none (the commands then act on the whole table).
    [[nodiscard]] ShowModel::TableRange cellSelection() const;
    void selectCells(const QString &shapeId, ShowModel::TableRange range);
    // In-place editing of the cell at (row, column) — any cell of its block.
    Q_INVOKABLE void beginCellEdit(const QString &shapeId, int row, int column);
    // Tab (+1) / Shift+Tab (-1) while a cell is edited: commits it and
    // edits the next cell in reading order; Tab in the last cell adds a
    // row first, as PowerPoint does. False when no cell is edited.
    Q_INVOKABLE bool moveCellEdit(int step);
    [[nodiscard]] bool editingCell() const { return m_textRow >= 0; }
    // The text look a table without any text types in (the deck's font and
    // colour; the host supplies it, the canvas holds no deck).
    void setTableTextLook(std::function<ShowModel::TextRun()> provider) { m_tableTextLook = std::move(provider); }
    // Applies `mutate` to shape `shapeId` as ONE undoable edit named
    // `label` (shapeEdited); a table's frame is refitted to its grid.
    // False, and nothing changed, when `mutate` returns false or changes
    // nothing.
    bool editShape(const QString &shapeId, const QString &label, const std::function<bool(ShowModel::Shape &)> &mutate);

    // ---- M82 (D-091): empty placeholders ----
    // What each EMPTY placeholder of the slide types in — its layout's look
    // (the host supplies it, the canvas holds no deck). Empty placeholders
    // show PowerPoint's prompt ("Click to add title") in that look, in the
    // editor only.
    void setPlaceholderLooks(QHash<QString, ShowModel::TextRun> looks);

    // ---- M75: the Slide Master view's placeholder frames ----
    // On: placeholders show dashed frames, as in PowerPoint's master view.
    void setTemplateMode(bool on);
    [[nodiscard]] bool templateMode() const { return m_templateMode; }

    // ---- in-place text editing (the window's TextEdit shows the document) ----
    Q_INVOKABLE void beginTextEdit(const QString &shapeId);
    Q_INVOKABLE void commitTextEdit(bool cancelled);
    [[nodiscard]] QString editingShapeId() const { return m_textShapeId; }
    [[nodiscard]] QRectF editorRect() const;
    [[nodiscard]] QTextDocument *editorDocument() const { return m_textDocument; }

    void paint(QPainter *painter) override;

signals:
    void shapeInserted(const QString &shapeId);
    void shapeRemoved(int index, const ShowModel::Shape &removed);
    void shapeGeometryEdited(const QString &shapeId, const QRectF &oldRect, const QRectF &newRect);
    void shapeTextEdited(const QString &shapeId, const QList<ShowModel::TextParagraph> &oldText,
                         const QList<ShowModel::TextParagraph> &newText);
    void shapeBuildEdited(const QString &shapeId, int oldStep, int newStep);
    void shapesReordered(const QStringList &oldOrder, const QStringList &newOrder);
    void shapeStyled(const QString &shapeId, const ShowModel::Shape &before, const ShowModel::Shape &after);
    // M67: a whole-shape edit (tables, cells, chart data) named `label`.
    void shapeEdited(const QString &shapeId, const ShowModel::Shape &before, const ShowModel::Shape &after,
                     const QString &label);
    void cellSelectionChanged();
    void selectionChanged();
    void slideEdited(); // any of the above; the host marks dirty + journals
    // A gesture that touches SEVERAL shapes is starting/finishing: the host
    // snapshots the slide on the first and pushes ONE command on the second.
    void compoundEditStarted();
    void compoundEditFinished();
    void editingChanged();
    void snapChanged();
    // A right press at item (x, y).
    void contextMenuRequested(qreal x, qreal y);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;

private:
    enum class Gesture { None, Move, Resize, Marquee, Cells, ColumnBoundary, RowBoundary };

    // slide_canvas_item_marks.cpp (M75): placeholder frames in the Slide
    // Master view, the slide's comment markers, the play mark of media —
    // the editor's own marks, never in the show, thumbnails or print.
    void paintMarks(QPainter *painter) const;

    // slide_canvas_item_table.cpp
    bool tablePress(const QPointF &pos, Qt::KeyboardModifiers modifiers);
    bool tableMove(const QPointF &pos);
    bool tableRelease();
    bool tableKey(QKeyEvent *event);
    void paintTableSelection(QPainter *painter) const;
    [[nodiscard]] ShowModel::Shape paintedWhileEditing(const ShowModel::Shape &shape) const;
    [[nodiscard]] QRectF cellEditorRect(const ShowModel::Shape &shape) const;
    void commitCellEdit(bool cancelled);
    void dropCellSelectionIfGone();

    void reorderTo(const QStringList &order);
    [[nodiscard]] int shapeIndex(const QString &shapeId) const;
    [[nodiscard]] QStringList shapeOrder() const;
    void notifySelection();
    void finishGesture();

    ShowModel::Slide *m_slide = nullptr;
    QSizeF m_slideSize{720.0, 405.0};
    ShowModel::SlideMaster m_master;
    bool m_hasMaster = false;
    ShowModel::SlideBackdrop m_backdrop;
    QSet<QString> m_selected;
    // Gesture state (slide points).
    Gesture m_gesture = Gesture::None;
    QPointF m_pressPos;
    QString m_draggingShapeId;
    QHash<QString, QRectF> m_gestureStart;
    QRectF m_marquee;
    bool m_compound = false;
    bool m_moved = false;
    // Text editing.
    QTextDocument *m_textDocument = nullptr;
    QString m_textShapeId;
    QList<ShowModel::TextParagraph> m_textOriginal;
    // Snapping.
    bool m_snap = true;
    QList<QLineF> m_guides;
    // Tables (M67): the selected cells, the cell being edited, a boundary drag.
    QString m_tableId;
    ShowModel::TableRange m_cells{-1, -1, -1, -1};
    ShowModel::TableRange m_cellAnchor{-1, -1, -1, -1};
    int m_textRow = -1;
    int m_textColumn = -1;
    int m_boundary = -1;
    ShowModel::Shape m_tableBefore;
    std::function<ShowModel::TextRun()> m_tableTextLook;
    bool m_templateMode = false;
    QHash<QString, ShowModel::TextRun> m_placeholderLooks; // M82
};

} // namespace QindaOffice::Apps::Show
