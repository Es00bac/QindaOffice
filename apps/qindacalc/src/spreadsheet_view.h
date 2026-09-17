// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc grid (plan section 6.2): a custom QAbstractScrollArea over
// the sparse engine, painting only visible cells so 100k-cell sheets stay
// responsive. Single rectangular selection, keyboard navigation, in-place
// line-edit editing, row/column headers, and a formula bar docked above.

#include "value.h"
#include "workbook.h"

#include <QFont>
#include <QAbstractScrollArea>

class QLineEdit;

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;

class SpreadsheetView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SpreadsheetView(SpreadsheetDocument *document, QWidget *parent = nullptr);

    [[nodiscard]] CalcEng::CellAddr anchor() const { return m_anchor; }
    [[nodiscard]] CalcEng::CellAddr cursorCell() const { return m_cursor; }
    void setCursorCell(const CalcEng::CellAddr &addr, bool extendSelection = false);
    // Normalized anchor..cursor rectangle (selection for commands).
    [[nodiscard]] CalcEng::CellRange selectionRange() const;
    // The populated block of the active sheet, or an invalid range when the
    // sheet is empty. Shared by Select All and the print paginator.
    [[nodiscard]] CalcEng::CellRange usedRange() const;
    // View zoom (View > Zoom In/Out/Reset). Presentation state only: it
    // scales the rendered geometry and the cell font, never the stored
    // column widths or row heights, so zooming never dirties the document.
    void setZoom(qreal zoom);
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    // The cursor cell in VIEWPORT coordinates (grid position plus the header
    // offset). AGENT-GUARD: cellRect() is in GRID coordinates. The grid is
    // painted under p.translate(kHeaderWidth, kHeaderHeight), so inside that
    // block cellRect() is already correct and adding the offset again pushes
    // the drawing one header down and right. Use this accessor only outside
    // the painter transform — scroll-into-view math and the inline editor's
    // geometry — and never inside it.
    [[nodiscard]] QRect cursorViewportRect() const;
    // Extends the selection to cover every used cell (Select All).
    void selectUsedRange();
    // Whole-row / whole-column selection from the headers. AGENT-GUARD: the
    // selection is bounded by the USED range, never by kMaxRows/kMaxCols —
    // range commands (clear, restyle, merge) iterate every cell in the
    // selection, and a literal 1,048,576-row selection would hang the UI
    // and mint a style object per cell.
    void selectColumn(qint32 col, bool extendSelection);
    void selectRow(qint32 row, bool extendSelection);
    // Widest rendered text in the column, clamped to sane bounds (the
    // double-click-the-boundary gesture and Format > Optimal Width).
    [[nodiscard]] qreal autofitColumnWidth(qint32 col) const;
    // Freeze panes: rows above `row` and columns left of `col` stay put
    // while the rest scrolls. {0,0} means nothing is frozen.
    void setFrozen(qint32 row, qint32 col);
    [[nodiscard]] CalcEng::CellAddr frozen() const { return m_frozen; }

signals:
    void cursorChanged(const CalcEng::CellAddr &addr, const QString &formulaText,
                       const QString &displayText);
    void editCommitted(const CalcEng::CellAddr &addr, const QString &input);
    void sheetCountChanged();
    // Any change to the selected block (cursor move, drag, header click).
    // The window turns this into the status bar's Sum/Average/Count.
    void selectionChanged(const CalcEng::CellRange &range);
    // Delete/Backspace over the selection. The view never mutates the
    // document itself — the window owns the undo step.
    void clearRequested(const CalcEng::CellRange &range);
    // Right-click inside the grid, with the cell under the pointer already
    // selected. The window fills and shows the context menu.
    void contextMenuRequested(const QPoint &globalPos);
    // A header drag finished: the window applies it as one undo step.
    void columnWidthChanged(qint32 col, qreal px);
    void rowHeightChanged(qint32 row, qreal px);

public slots:
    void commitFormulaBar(const QString &text);
    void refreshAll();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // One visible row or column: its index, its position in GRID
    // coordinates (viewport minus the header offset) and its size. Painting,
    // the header bands and hit-testing all walk the same two lists, which is
    // what keeps them agreeing — including across a frozen pane boundary,
    // where position is no longer a plain function of the scroll value.
    struct Band {
        qint32 index = 0;
        qreal pos = 0;
        qreal size = 0;
    };
    [[nodiscard]] QVector<Band> visibleRowBands() const;
    [[nodiscard]] QVector<Band> visibleColumnBands() const;
    [[nodiscard]] qreal frozenHeightPx() const;
    [[nodiscard]] qreal frozenWidthPx() const;

    void updateScrollBars();
    void startEditor();
    void commitEditor(bool moveDown);
    void cancelEditor();
    [[nodiscard]] QString inputTextAt(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QRect cellRect(const CalcEng::CellAddr &addr) const;
    // AGENT-GUARD: every geometry and hit-test path goes through these two,
    // never through Sheet::columnWidth()/rowHeight() directly — that is what
    // keeps painting and clicking agreeing at any zoom.
    [[nodiscard]] QFont scaledFont() const;
    [[nodiscard]] qreal colWidthPx(qint32 col) const;
    [[nodiscard]] qreal rowHeightPx(qint32 row) const;
    [[nodiscard]] CalcEng::CellAddr cellAtPos(const QPoint &pos) const;
    [[nodiscard]] int rowTop(qint32 row) const;
    [[nodiscard]] int colLeft(qint32 col) const;
    // Header hit-testing (spreadsheet_view_input.cpp). A negative result
    // means "not on a boundary"; the ±kGripPx band is what makes the
    // resize cursor and the drag agree.
    [[nodiscard]] qint32 columnBoundaryAt(int x) const;
    [[nodiscard]] qint32 rowBoundaryAt(int y) const;
    void emitSelectionChanged();
    void paintHeaderBands(QPainter &p, const QRect &clip);
    void paintGrid(QPainter &p, const QRect &clip);
    [[nodiscard]] QString displayTextAt(qint32 sheet, const CalcEng::CellAddr &addr) const;

    SpreadsheetDocument *m_document;
    QLineEdit *m_editor = nullptr;
    // AGENT-GUARD: a default-constructed CellAddr is {-1,-1}, which is not a
    // cell. The view used to start there, so before the first click the
    // selection was empty, cellValue() answered #REF! for the "cursor" and
    // anything reading the selection (the status bar's Count, the clipboard,
    // AutoSum) was working from a phantom address. A1 is the real start.
    CalcEng::CellAddr m_anchor{0, 0};
    CalcEng::CellAddr m_cursor{0, 0};
    // Top-left cell of the scrolling area: rows above and columns left of
    // it are frozen. {0,0} = nothing frozen.
    CalcEng::CellAddr m_frozen{0, 0};
    qreal m_zoom = 1.0;
    bool m_editing = false;
    // Header resize drag state; -1 when no drag is in progress.
    qint32 m_resizeColumn = -1;
    qint32 m_resizeRow = -1;
    qreal m_resizeOriginal = 0;
    int m_resizeOrigin = 0;

    static constexpr int kGripPx = 4; // boundary grab band in the headers
    static constexpr int kHeaderWidth = 52;
    static constexpr int kHeaderHeight = 24;
    static constexpr int kDefaultRowPx = 20;
    static constexpr qint32 kVisibleRowsBuffer = 4;
};

} // namespace QindaOffice::Apps::QindaCalc
