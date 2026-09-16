// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The note page surface (plan section 6.4): a fixed-width, growing page
// (height extends in page-height increments as content approaches the
// bottom, bounded at 64 page heights) with blank/ruled/grid/dotted
// backgrounds, pen/highlighter/eraser ink from tablet events (touch and
// cursor input is ignored - that IS the palm-rejection baseline), text
// blocks, and bounded snapshot undo.

#include "notebook.h"
#include "spatial_grid.h"
#include "stroke_builder.h"

#include <QWidget>

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

public slots:
    void undo();
    void addTextBlock();

signals:
    void pageEdited();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override; // tablet + mouse inking
    void resizeEvent(QResizeEvent *event) override;

private:
    void commitStroke();
    void rebuildGrid();
    void syncBlocks();
    void persistTextBlocks();
    void syncBlocksGeometry();
    void pushUndoSnapshot();
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
    QVector<Ink::StrokeLayer> m_undoStack;
    QList<QTextEdit *> m_textBlocks;
    qreal m_pageWidth = 760;
    qreal m_minPageHeight = 1004; // ~A4 ratio at the fixed width
    static constexpr qreal kMaxPages = 64;
};

} // namespace QindaOffice::Apps::Notes
