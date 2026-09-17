// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The note page surface (plan section 6.4): a fixed-width, growing page
// (height extends in page-height increments as content approaches the
// bottom, bounded at 64 page heights) with blank/ruled/grid/dotted
// backgrounds, pen/highlighter/eraser ink from tablet events (touch and
// cursor input is ignored - that IS the palm-rejection baseline), text
// blocks, and bounded snapshot undo.

#include "note_blocks.h"
#include "notebook.h"
#include "spatial_grid.h"
#include "stroke_builder.h"

#include <QHash>
#include <QWidget>

class QJsonObject;
class QTextEdit;

namespace QindaOffice::Apps::Notes {

enum class NoteTool { Pen, Highlighter, Eraser, Text };

class NotePageView final : public QWidget {
    Q_OBJECT
public:
    explicit NotePageView(QWidget *parent = nullptr);

    void setPage(NotePage *page);
    [[nodiscard]] NotePage *page() const { return m_page; }
    void setTool(NoteTool tool) { m_tool = tool; }
    [[nodiscard]] NoteTool tool() const { return m_tool; }

    // M15 polish: pen presentation + view zoom + page background.
    void setPenWidth(qreal width);
    void setPenColor(const QColor &color);
    [[nodiscard]] qreal penWidth() const { return m_penWidth; }
    [[nodiscard]] const QColor &penColor() const { return m_penColor; }
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

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override; // tablet + mouse inking
    void resizeEvent(QResizeEvent *event) override;

private:
    void commitStroke();
    void rebuildGrid();
    void rebuildBlockWidgets();
    void persistTextBlocks();
    void syncBlocksGeometry();
    void appendBlock(const QJsonObject &block);
    void paintImageBlocks(QPainter *painter, const QRectF &page) const;
    void pushUndoSnapshot();
    // Emits undoAvailable/redoAvailable for the current stack depths.
    void notifyHistoryAvailability();
    [[nodiscard]] QPointF toPagePos(const QPointF &widgetPos) const;
    [[nodiscard]] qreal growHeight() const;

    NotePage *m_page = nullptr;
    NoteTool m_tool = NoteTool::Pen;
    qreal m_penWidth = 2.5;
    QColor m_penColor = Qt::black; // black reads correctly on light paper
    qreal m_zoom = 1.0;
    Ink::StrokeBuilder m_builder;
    bool m_inking = false;
    bool m_syncingBlocks = false;
    NoteTool m_toolBeforeEraser = NoteTool::Pen; // tool restored after eraser-end flips
    Ink::SpatialGrid m_grid{64.0};
    // AGENT-GUARD: undo and redo are two halves of ONE history. Every new
    // edit clears m_redoStack — a redo stack that survives a fresh edit
    // replays strokes onto a page that has moved on. The view had an undo
    // stack and no inverse at all, so Edit ▸ Redo sat permanently disabled.
    QVector<Ink::StrokeLayer> m_undoStack;
    QVector<Ink::StrokeLayer> m_redoStack;
    static constexpr int kMaxHistory = 100;
    // Editable blocks only: images are painted, so they have no widget.
    QList<QWidget *> m_blockWidgets;
    QHash<QWidget *, int> m_blockIndexForWidget;
    QList<PageBlock> m_blocks;
    qreal m_pageWidth = 760;
    qreal m_minPageHeight = 1004; // ~A4 ratio at the fixed width
    static constexpr qreal kMaxPages = 64;
};

} // namespace QindaOffice::Apps::Notes
