// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc grid as a QtQuick item (conversion plan §5.2): a
// QQuickPaintedItem over the sparse engine, painting only the visible
// cells so 100k-cell sheets stay responsive. Single rectangular selection,
// keyboard navigation, row/column headers with boundary drags, frozen
// panes, autofilter buttons, comment markers; the in-cell editor is a
// Tk.TextField the QML grid places at `editorRect` and commits through
// commitEditor(). Scrolling is the item's own `scrollX/scrollY`, bound to
// two Tk.ScrollBars in CalcGrid.qml.
//
// AGENT-CONTRACT: the item never mutates the document except for the live
// header-resize feedback, which is rolled back before the signal
// (spreadsheet_item_input.cpp). Cell content is painted in fixed document
// colours (paper white, black text: D-004); the chrome colours (headers,
// selection, cursor, grid lines) are properties the QML binds to Tk.Theme.
//
// Files: spreadsheet_item.cpp (geometry, cursor, scroll, editor contract),
// spreadsheet_item_paint.cpp, spreadsheet_item_input.cpp.

#include "pivot_style.h"
#include "value.h"
#include "workbook.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QQuickPaintedItem>
#include <QSharedPointer>
#include <QtQml/qqmlregistration.h>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;

class SpreadsheetItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int cursorRow READ cursorRow NOTIFY cursorChanged)
    Q_PROPERTY(int cursorCol READ cursorCol NOTIFY cursorChanged)
    Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorChanged)
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY cursorChanged)
    Q_PROPERTY(QString formulaText READ formulaText NOTIFY cursorChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal scrollX READ scrollX WRITE setScrollX NOTIFY scrollChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollChanged)
    // Total scrollable extent (px at the current zoom) and the visible
    // part of it, for the scroll bars.
    Q_PROPERTY(qreal scrollWidth READ scrollWidth NOTIFY scrollChanged)
    Q_PROPERTY(qreal scrollHeight READ scrollHeight NOTIFY scrollChanged)
    Q_PROPERTY(qreal viewportWidth READ viewportWidth NOTIFY scrollChanged)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight NOTIFY scrollChanged)
    Q_PROPERTY(bool editing READ isEditing NOTIFY editingChanged)
    Q_PROPERTY(QRectF editorRect READ editorRect NOTIFY editingChanged)
    Q_PROPERTY(QString editorText READ editorText NOTIFY editingChanged)
    Q_PROPERTY(bool editorSelectAll READ editorSelectAll NOTIFY editingChanged)
    Q_PROPERTY(QFont cellFont READ cellFont WRITE setCellFont NOTIFY cellFontChanged)
    Q_PROPERTY(QString hoverComment READ hoverComment NOTIFY hoverChanged)
    Q_PROPERTY(QPointF hoverPoint READ hoverPoint NOTIFY hoverChanged)
    // Chrome colours (Tk.Theme roles, bound from QML).
    Q_PROPERTY(QColor headerColor READ headerColor WRITE setHeaderColor NOTIFY chromeChanged)
    Q_PROPERTY(QColor headerTextColor READ headerTextColor WRITE setHeaderTextColor NOTIFY chromeChanged)
    Q_PROPERTY(QColor headerLineColor READ headerLineColor WRITE setHeaderLineColor NOTIFY chromeChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY chromeChanged)
    Q_PROPERTY(QColor cursorColor READ cursorColor WRITE setCursorColor NOTIFY chromeChanged)
public:
    // What the selection was made of (M66): Data ▸ Group groups the
    // columns of a selection made from the column headers, else rows.
    enum SelectionKind { CellSelection, RowSelection, ColumnSelection };
    Q_ENUM(SelectionKind)

    explicit SpreadsheetItem(QQuickItem *parent = nullptr);

    void setDocument(SpreadsheetDocument *document);
    [[nodiscard]] SpreadsheetDocument *document() const { return m_document; }

    // ---- cursor and selection ----
    [[nodiscard]] CalcEng::CellAddr anchor() const { return m_anchor; }
    [[nodiscard]] CalcEng::CellAddr cursorCell() const { return m_cursor; }
    [[nodiscard]] int cursorRow() const { return m_cursor.row; }
    [[nodiscard]] int cursorCol() const { return m_cursor.col; }
    [[nodiscard]] QString cursorText() const;
    [[nodiscard]] QString selectionText() const;
    [[nodiscard]] QString formulaText() const;
    Q_INVOKABLE void setCursorCell(int row, int col, bool extendSelection = false);
    void setCursorCell(const CalcEng::CellAddr &addr, bool extendSelection = false);
    // Normalized anchor..cursor rectangle (selection for commands).
    [[nodiscard]] CalcEng::CellRange selectionRange() const;
    [[nodiscard]] SelectionKind selectionKind() const { return m_selectionKind; }
    // The populated block of the active sheet, or an invalid range.
    [[nodiscard]] CalcEng::CellRange usedRange() const;
    Q_INVOKABLE void selectUsedRange();
    // Whole-row / whole-column selection, bounded by the USED range (a
    // literal million-row selection would hang every range command).
    Q_INVOKABLE void selectColumn(int col, bool extendSelection);
    Q_INVOKABLE void selectRow(int row, bool extendSelection);

    // ---- view ----
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    [[nodiscard]] qreal scrollX() const { return m_scrollX; }
    void setScrollX(qreal value);
    [[nodiscard]] qreal scrollY() const { return m_scrollY; }
    void setScrollY(qreal value);
    [[nodiscard]] qreal scrollWidth() const { return m_scrollWidth; }
    [[nodiscard]] qreal scrollHeight() const { return m_scrollHeight; }
    [[nodiscard]] qreal viewportWidth() const;
    [[nodiscard]] qreal viewportHeight() const;
    // Semantic projection of the exact bands used by painting/hit-testing,
    // including frozen panes and filtered rows. Indices are zero-based.
    [[nodiscard]] QList<qint32> visibleRows() const
    {
        QList<qint32> rows;
        for (const auto &band : visibleRowBands()) rows.append(band.index);
        return rows;
    }
    [[nodiscard]] QList<qint32> visibleColumns() const
    {
        QList<qint32> columns;
        for (const auto &band : visibleColumnBands()) columns.append(band.index);
        return columns;
    }
    // Freeze state is DOCUMENT state (Sheet::freezeRows/Cols — it
    // round-trips to xlsx and rides undo/recovery); the item reads through.
    [[nodiscard]] CalcEng::CellAddr frozen() const;
    [[nodiscard]] int frozenRow() const { return frozen().row; }
    [[nodiscard]] int frozenCol() const { return frozen().col; }
    // The cursor cell in ITEM coordinates (grid position plus the header
    // offset); cellRect() is in GRID coordinates (inside the painter's
    // translate). Use this outside the painter only.
    [[nodiscard]] QRectF cursorItemRect() const;
    Q_INVOKABLE qreal autofitColumnWidth(int col) const;
    // Test seam: the grid-space top of a row (collapses onto the row above
    // when a filter hides it).
    Q_INVOKABLE int rowTopForTest(int row) const { return rowTop(row); }
    Q_INVOKABLE int colLeftForTest(int col) const { return colLeft(col); }
    Q_INVOKABLE void refreshAll();
    [[nodiscard]] QFont cellFont() const { return m_font; }
    void setCellFont(const QFont &font);

    // ---- the editor contract (QML places a Tk.TextField at editorRect) ----
    [[nodiscard]] bool isEditing() const { return m_editing; }
    [[nodiscard]] QRectF editorRect() const;
    [[nodiscard]] QString editorText() const { return m_editorText; }
    [[nodiscard]] bool editorSelectAll() const { return m_editorSelectAll; }
    Q_INVOKABLE void startEditor();
    // A printable key while navigating: the editor opens with that text.
    Q_INVOKABLE void startEditorWith(const QString &text);
    // Commits `text`; emits editCommitted only when it differs from the
    // cell's input. `moveDown` is Return's commit-and-advance.
    Q_INVOKABLE void commitEditor(const QString &text, bool moveDown);
    Q_INVOKABLE void cancelEditor();
    Q_INVOKABLE void stepCursor(int rowDelta, int colDelta);
    // The formula bar commits through the same contract.
    Q_INVOKABLE void commitFormulaBar(const QString &text);

    // ---- hover (comment tooltip) ----
    [[nodiscard]] QString hoverComment() const { return m_hoverComment; }
    [[nodiscard]] QPointF hoverPoint() const { return m_hoverPoint; }

    // ---- chrome colours ----
    [[nodiscard]] QColor headerColor() const { return m_headerColor; }
    void setHeaderColor(const QColor &c) { setChrome(m_headerColor, c); }
    [[nodiscard]] QColor headerTextColor() const { return m_headerTextColor; }
    void setHeaderTextColor(const QColor &c) { setChrome(m_headerTextColor, c); }
    [[nodiscard]] QColor headerLineColor() const { return m_headerLineColor; }
    void setHeaderLineColor(const QColor &c) { setChrome(m_headerLineColor, c); }
    [[nodiscard]] QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor &c) { setChrome(m_selectionColor, c); }
    [[nodiscard]] QColor cursorColor() const { return m_cursorColor; }
    void setCursorColor(const QColor &c) { setChrome(m_cursorColor, c); }

    static constexpr int kHeaderWidth = 52;
    static constexpr int kHeaderHeight = 24;
    // The outline gutter (M66, D-075): one step per level, beside the row
    // headers and above the column headers, only when the sheet is grouped.
    static constexpr int kGutterStep = 14;
    [[nodiscard]] qreal rowGutterPx() const;
    [[nodiscard]] qreal colGutterPx() const;
    // Where the grid starts in item coordinates: the headers plus the gutter.
    [[nodiscard]] qreal headerWidth() const { return rowGutterPx() + kHeaderWidth; }
    [[nodiscard]] qreal headerHeight() const { return colGutterPx() + kHeaderHeight; }

signals:
    void cursorChanged();
    // Any change to the selected block (cursor move, drag, header click).
    void selectionChanged(const CalcEng::CellRange &range);
    void editCommitted(const CalcEng::CellAddr &addr, const QString &input);
    // Delete/Backspace over the selection; the host owns the undo step.
    void clearRequested(const CalcEng::CellRange &range);
    // Right-click inside the grid (item coordinates), the cell under the
    // pointer already selected. The window shows the context menu.
    void contextMenuRequested(qreal x, qreal y);
    // A header drag finished: the host applies it as one undo step.
    void columnWidthChanged(int col, qreal px);
    void rowHeightChanged(int row, qreal px);
    // The autofilter dropdown in a header cell was clicked (item coords).
    void filterButtonClicked(int column, qreal x, qreal y);
    // M66: Ctrl+click on a cell with a link; the outline gutter's level
    // buttons and +/- buttons (the host changes the document).
    void linkActivated(int row, int col);
    // M74: a double click on a PivotTable's output (Excel: Show Details on a
    // value) — the editor does not open there.
    void pivotCellActivated(int row, int col);
    void outlineLevelClicked(bool rows, int level);
    void outlineToggled(bool rows, int line, int level);
    void zoomChanged();
    void scrollChanged();
    void editingChanged();
    void cellFontChanged();
    void hoverChanged();
    void chromeChanged();

protected:
    void paint(QPainter *painter) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // One visible row or column: index, GRID position and size. Painting,
    // the header bands and hit-testing all walk the same two lists.
    struct Band {
        qint32 index = 0;
        qreal pos = 0;
        qreal size = 0;
    };
    [[nodiscard]] QVector<Band> visibleRowBands() const;
    [[nodiscard]] QVector<Band> visibleColumnBands() const;
    [[nodiscard]] qreal frozenHeightPx() const;
    [[nodiscard]] qreal frozenWidthPx() const;
    void updateScrollRanges();
    [[nodiscard]] QString inputTextAt(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QRectF cellRect(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QFont scaledFont() const;
    [[nodiscard]] qreal colWidthPx(qint32 col) const;
    [[nodiscard]] qreal rowHeightPx(qint32 row) const;
    [[nodiscard]] qreal visibleRowHeightPx(qint32 row, const QSet<qint32> &hidden) const;
    [[nodiscard]] CalcEng::CellAddr cellAtPos(const QPointF &gridPos) const;
    [[nodiscard]] int rowTop(qint32 row) const;
    [[nodiscard]] int colLeft(qint32 col) const;
    [[nodiscard]] qint32 columnBoundaryAt(qreal x) const;
    [[nodiscard]] qint32 rowBoundaryAt(qreal y) const;
    [[nodiscard]] qint32 filterButtonAt(const QPointF &gridPos) const;
    [[nodiscard]] const CalcEng::Sheet *activeSheet() const;
    // Decoded pictures, keyed by their shared bytes. The entry keeps the
    // byte array alive, so an address can never be recycled under a stale
    // cache entry; without the decode cache a scroll would re-decode every
    // photo on the sheet, every frame.
    struct CachedImage {
        QSharedPointer<const QByteArray> bytes;
        QImage image;
    };
    mutable QHash<quintptr, CachedImage> m_imageCache;
    void emitSelectionChanged();
    void paintHeaderBands(QPainter &p);
    void paintImages(QPainter &p);
    // spreadsheet_item_outline.cpp (M66): the gutter, the link look and
    // the spill border, and stepping over hidden rows and columns.
    void paintOutline(QPainter &p);
    void paintSpillBorder(QPainter &p);
    [[nodiscard]] bool outlineClick(const QPointF &pos);
    [[nodiscard]] bool hasLink(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QString linkTip(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] qint32 visibleStep(qint32 from, int delta, bool rows) const;
    // spreadsheet_item_pivot.cpp (M74): the PivotTable look of a cell, its
    // rules, the empty pivot's placeholder; the theme's colours, cached.
    [[nodiscard]] const QVector<CalcEng::Rgb> &pivotTheme() const;
    [[nodiscard]] std::optional<CalcEng::PivotCellLook> pivotLookAt(const CalcEng::Sheet &sheet,
                                                                    const CalcEng::CellAddr &addr) const;
    void paintPivotRules(QPainter &p, const QRectF &cell, const CalcEng::PivotCellLook &look) const;
    void paintPivotPlaceholders(QPainter &p);
    mutable QVector<CalcEng::Rgb> m_pivotTheme;
    mutable QByteArray m_pivotThemeKey;
    void paintGrid(QPainter &p);
    void setChrome(QColor &slot, const QColor &value);

    SpreadsheetDocument *m_document = nullptr;
    // AGENT-GUARD: A1, never {-1,-1} — the selection readouts describe a
    // phantom cell otherwise.
    CalcEng::CellAddr m_anchor{0, 0};
    CalcEng::CellAddr m_cursor{0, 0};
    SelectionKind m_selectionKind = CellSelection;
    qreal m_zoom = 1.0;
    qreal m_scrollX = 0;
    qreal m_scrollY = 0;
    qreal m_scrollWidth = 0;
    qreal m_scrollHeight = 0;
    QFont m_font;
    bool m_editing = false;
    QString m_editorText;
    bool m_editorSelectAll = true;
    QString m_hoverComment;
    QPointF m_hoverPoint;
    qint32 m_resizeColumn = -1;
    qint32 m_resizeRow = -1;
    qreal m_resizeOriginal = 0;
    qreal m_resizeOrigin = 0;
    QColor m_headerColor{0xe8, 0xe8, 0xec};
    QColor m_headerTextColor{0x30, 0x30, 0x38};
    QColor m_headerLineColor{0xb0, 0xb0, 0xb8};
    QColor m_selectionColor{0x3d, 0x7b, 0xe0};
    QColor m_cursorColor{0x2a, 0x5b, 0xb0};

    static constexpr int kGripPx = 4;
    static constexpr int kFilterButtonPx = 16;
    static constexpr int kDefaultRowPx = 20;
};

} // namespace QindaOffice::Apps::QindaCalc
