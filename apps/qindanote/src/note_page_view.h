// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The note page surface (plan section 6.4, M22 overhaul): a fixed-width,
// growing page (height extends in page-height increments as content
// approaches the bottom, bounded at 64 page heights) with
// blank/ruled/grid/dotted backgrounds and pen/highlighter/eraser ink from
// tablet events (touch input is ignored — that IS the palm-rejection
// baseline). Ink renders as filled antialiased outlines through
// office_ink; the eraser acts live during the drag; one QUndoStack is the
// whole page history (note_page_history.h).
//
// Input handling (tablet/mouse/wheel/keys, cursors, barrel buttons,
// middle-drag pan) lives in note_page_input.cpp; content blocks in
// note_page_blocks.cpp; this header is the shared state for all three.

#include "note_blocks.h"
#include "notebook.h"
#include "spatial_grid.h"
#include "stroke_builder.h"
#include "stroke_edit.h"

#include <QHash>
#include <QWidget>

class QJsonObject;
class QScrollArea;
class QTextEdit;
class QUndoStack;

namespace QindaOffice::Apps::Notes {

enum class NoteTool { Pen, Highlighter, Eraser, Text };

class NotePageView final : public QWidget {
    Q_OBJECT
public:
    explicit NotePageView(QWidget *parent = nullptr);

    void setPage(NotePage *page);
    [[nodiscard]] NotePage *page() const { return m_page; }
    void setTool(NoteTool tool);
    [[nodiscard]] NoteTool tool() const { return m_tool; }

    // Tool state. Pen width/color are document-facing presentation state
    // (D-009); NoteInkActions owns the actions and applies them per tool.
    void setPenWidth(qreal width);
    void setPenColor(const QColor &color);
    [[nodiscard]] qreal penWidth() const { return m_penWidth; }
    [[nodiscard]] const QColor &penColor() const { return m_penColor; }
    void setHighlighterColor(const QColor &color);
    [[nodiscard]] const QColor &highlighterColor() const { return m_highlighterColor; }
    void setHighlighterWidth(qreal width);
    [[nodiscard]] qreal highlighterWidth() const { return m_highlighterWidth; }
    void setEraserMode(Ink::EraseMode mode);
    [[nodiscard]] Ink::EraseMode eraserMode() const { return m_eraserMode; }
    // The eraser disc's radius in page units; the cursor ring is painted
    // at radius x zoom.
    void setEraserRadius(qreal radius);
    [[nodiscard]] qreal eraserRadius() const { return m_eraserRadius; }
    // Preferences note/pressure + note/smoothing, applied per sample.
    void setPressureEnabled(bool on) { m_pressureEnabled = on; }
    void setStrokeSmoothing(qreal factor);
    void setZoom(qreal zoom);
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    // Sets the page background style (blank|ruled|grid|dotted); edits the
    // live envelope and reports the page as edited (window persists).
    void setBackground(const QString &background);
    // The page rect in widget coordinates (zoom-aware); public for the
    // window's layout math and tests.
    [[nodiscard]] QRectF pageRect() const;
    // Resizes the widget to the (zoomed, grown) page. The window hosts the
    // view in a QScrollArea, so this is what makes zoom and page growth
    // reachable instead of clipped.
    void updatePageMetrics();

    // Applies an undo/redo snapshot (InkCommand calls this): the layer
    // replaces the page's ink and the edit is reported like a fresh one.
    void applyHistoryLayer(const Ink::StrokeLayer &layer);

public slots:
    void undo();
    void redo();
    // Content blocks (note_page_blocks.cpp). Every one lands at the bottom
    // of the page's block stack and is persisted into the envelope.
    void addTextBlock();
    void addCheckItem();
    void addImageBlock(const QByteArray &imageData);

signals:
    void pageEdited();
    // Availability of the two history commands, so the window's Edit ▸ Undo
    // and Edit ▸ Redo read the real state instead of guessing.
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    // Tool, colour, width or zoom changed — the window refreshes the
    // status field and any presentation pickers from this.
    void presentationChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override; // tablet + mouse inking (note_page_input.cpp)
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override; // note_page_input.cpp
    void keyPressEvent(QKeyEvent *event) override; // note_page_input.cpp

private:
    void commitStroke(); // pen/highlighter release (note_page_input.cpp)
    // Live eraser gestures (note_page_input.cpp): snapshot at press, erase
    // per sample, ONE InkCommand at release — and only when anything was
    // actually erased.
    void beginEraseGesture(const QPointF &pagePos);
    void eraseAtPagePos(const QPointF &pagePos);
    void endEraseGesture();
    // Eraser-end flips and barrel buttons borrow a tool for one gesture.
    void beginTemporaryTool(NoteTool tool);
    void restoreTemporaryTool();
    // The scroll area hosting the view, found up the parent chain — pan
    // and Ctrl+wheel zoom scroll it without coupling to NoteWindow.
    [[nodiscard]] QScrollArea *scrollArea() const;
    void updateCursor();
    [[nodiscard]] QCursor eraserCursor() const;
    void rebuildGrid();
    void rebuildBlockWidgets();
    void persistTextBlocks();
    void syncBlocksGeometry();
    void appendBlock(const QJsonObject &block);
    void paintImageBlocks(QPainter *painter, const QRectF &page) const;
    [[nodiscard]] QPointF toPagePos(const QPointF &widgetPos) const;
    // Page-space rect → widget repaint rect (the live-eraser and inking
    // paths repaint ONLY this, never the whole page per sample).
    [[nodiscard]] QRect toWidgetRect(const QRectF &pageSpace) const;
    [[nodiscard]] qreal growHeight() const;

    NotePage *m_page = nullptr;
    NoteTool m_tool = NoteTool::Pen;
    qreal m_penWidth = 2.5;
    QColor m_penColor = Qt::black; // black reads correctly on light paper
    QColor m_highlighterColor{255, 210, 0};
    qreal m_highlighterWidth = 12.0;
    Ink::EraseMode m_eraserMode = Ink::EraseMode::Stroke;
    qreal m_eraserRadius = 12.0;
    bool m_pressureEnabled = true;
    qreal m_smoothing = 0.25;
    qreal m_zoom = 1.0;
    Ink::StrokeBuilder m_builder;
    bool m_inking = false;
    bool m_erasing = false;
    bool m_eraseChanged = false;
    Ink::StrokeLayer m_eraseSnapshot;
    bool m_temporaryTool = false;
    NoteTool m_toolBeforeTemporary = NoteTool::Pen; // restored after eraser-end/barrel flips
    bool m_panning = false;
    QPoint m_panLast;
    bool m_syncingBlocks = false;
    Ink::SpatialGrid m_grid{64.0};
    QUndoStack *m_history = nullptr; // the ONE page history (note_page_history.h)
    // Editable blocks only: images are painted, so they have no widget.
    QList<QWidget *> m_blockWidgets;
    QHash<QWidget *, int> m_blockIndexForWidget;
    QList<PageBlock> m_blocks;
    qreal m_pageWidth = 760;
    qreal m_minPageHeight = 1004; // ~A4 ratio at the fixed width
    static constexpr qreal kMaxPages = 64;
};

} // namespace QindaOffice::Apps::Notes
