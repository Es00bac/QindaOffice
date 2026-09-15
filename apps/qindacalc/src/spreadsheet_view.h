// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc grid (plan section 6.2): a custom QAbstractScrollArea over
// the sparse engine, painting only visible cells so 100k-cell sheets stay
// responsive. Single rectangular selection, keyboard navigation, in-place
// line-edit editing, row/column headers, and a formula bar docked above.

#include "value.h"
#include "workbook.h"

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
    // Extends the selection to cover every used cell (Select All).
    void selectUsedRange();

signals:
    void cursorChanged(const CalcEng::CellAddr &addr, const QString &formulaText,
                       const QString &displayText);
    void editCommitted(const CalcEng::CellAddr &addr, const QString &input);
    void sheetCountChanged();

public slots:
    void commitFormulaBar(const QString &text);
    void refreshAll();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateScrollBars();
    void startEditor();
    void commitEditor(bool moveDown);
    void cancelEditor();
    [[nodiscard]] QString inputTextAt(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QRect cellRect(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] CalcEng::CellAddr cellAtPos(const QPoint &pos) const;
    [[nodiscard]] int rowTop(qint32 row) const;
    [[nodiscard]] int colLeft(qint32 col) const;
    void paintHeaderBands(QPainter &p, const QRect &clip);
    void paintGrid(QPainter &p, const QRect &clip);
    [[nodiscard]] QString displayTextAt(qint32 sheet, const CalcEng::CellAddr &addr) const;

    SpreadsheetDocument *m_document;
    QLineEdit *m_editor = nullptr;
    CalcEng::CellAddr m_anchor;
    CalcEng::CellAddr m_cursor;
    bool m_editing = false;

    static constexpr int kHeaderWidth = 52;
    static constexpr int kHeaderHeight = 24;
    static constexpr int kDefaultRowPx = 20;
    static constexpr qint32 kVisibleRowsBuffer = 4;
};

} // namespace QindaOffice::Apps::QindaCalc
