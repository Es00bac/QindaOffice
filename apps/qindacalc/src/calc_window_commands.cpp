// SPDX-License-Identifier: GPL-3.0-or-later
// CalcWindow command implementations: the print paginator, name-box
// navigation/definition and the template-load consent. Split out of
// calc_window.cpp to keep every file under the line budget (AGENTS.md),
// same pattern as document_window_persistence.cpp and show_window_actions.cpp.

#include "calc_window.h"

#include "calc_find_bar.h"
#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include <QAction>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabBar>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
// A name the workbook can hold: identifier-shaped and not something the
// engine would read as a reference (or the name would shadow a real cell).
bool isDefinableName(const QString &text)
{
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    if (!shape.match(text).hasMatch() || text.size() > 255) {
        return false;
    }
    const ParseResult parsed = parseFormula(text);
    return !(parsed.root && parsed.root->kind == Node::Kind::Ref);
}
} // namespace

Shell::PrintRenderCallback CalcWindow::printRenderCallback() const
{
    // Snapshot what to print now; the render runs synchronously inside the
    // print dialog, so the document cannot move under it.
    const SpreadsheetDocument *doc = m_document;
    const qint32 sheetIndex = m_document->activeSheet();
    const CellRange used = m_view->usedRange();
    return [doc, sheetIndex, used](QPrinter *printer, QPainter *painter) {
        if (!used.isValid()) {
            return;
        }
        const Sheet &sheet = doc->workbook()->sheetAt(sheetIndex);
        const QRectF page(0, 0, printer->width(), printer->height());
        // Paginate the used range by rows, fitting the columns to the page
        // width. A row never splits across pages.
        qreal totalWidth = 0;
        for (qint32 col = used.topLeft.col; col <= used.bottomRight.col; ++col) {
            totalWidth += sheet.columnWidth(col);
        }
        const qreal scale = totalWidth > 0
            ? qMin<qreal>(1.0, page.width() / totalWidth)
            : 1.0;
        painter->save();
        painter->scale(scale, scale);
        const qreal pageHeight = page.height() / scale;
        qreal y = 0;
        for (qint32 row = used.topLeft.row; row <= used.bottomRight.row; ++row) {
            const qreal h = sheet.rowHeight(row);
            if (y > 0 && y + h > pageHeight) {
                printer->newPage();
                y = 0;
            }
            qreal x = 0;
            for (qint32 col = used.topLeft.col; col <= used.bottomRight.col; ++col) {
                const qreal w = sheet.columnWidth(col);
                const QRectF cell(x, y, w, h);
                painter->drawRect(cell);
                const QString text = doc->displayTextAt(sheetIndex, {row, col});
                if (!text.isEmpty()) {
                    const CalcEng::Cell *data = sheet.cellAt({row, col});
                    const Qt::Alignment align = Qt::AlignVCenter
                        | ((data != nullptr
                            && (isNumber(data->value) || isBool(data->value)))
                               ? Qt::AlignRight
                               : Qt::AlignLeft);
                    painter->drawText(cell.adjusted(3, 0, -3, 0), int(align), text);
                }
                x += w;
            }
            y += h;
        }
        painter->restore();
    };
}

void CalcWindow::goToRange(const CalcEng::RangeRef &ref)
{
    // A qualified reference switches sheets first; the tab bar's
    // currentChanged handler repaints and re-anchors the view.
    if (!ref.sheet.isEmpty()) {
        const qint32 sheet = m_document->workbook()->sheetIndex(ref.sheet);
        if (sheet >= 0 && sheet != m_document->activeSheet()) {
            m_tabs->setCurrentIndex(int(sheet));
        }
    }
    m_view->setCursorCell(ref.range.topLeft);
    if (ref.range.topLeft != ref.range.bottomRight) {
        m_view->setCursorCell(ref.range.bottomRight, /*extendSelection=*/true);
    }
}

void CalcWindow::commitNameBox(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    // Name-box semantics, as in every other spreadsheet: a reference or an
    // existing name navigates; anything else that is a legal name defines
    // one over the current selection. The box used to accept single-cell
    // references only and silently swallow everything else, which left
    // named ranges with no way to be created at all.
    const ParseResult parsed = parseFormula(text);
    if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
        goToRange(parsed.root->ref);
        return;
    }
    const auto existing = m_document->workbook()->namedRange(text.toUpper());
    if (existing.has_value()) {
        goToRange(*existing);
        statusBar()->showMessage(tr("%1 = %2").arg(text.toUpper(),
                                                   rangeRefText(*existing)), 5000);
        return;
    }
    if (!isDefinableName(text)) {
        statusBar()->showMessage(
            tr("“%1” is not a cell reference or a usable name.").arg(text), 8000);
        m_view->setFocus();
        return;
    }
    RangeRef target;
    target.range = m_view->selectionRange();
    m_document->defineName(text, target);
    statusBar()->showMessage(tr("Named %1 as “%2”")
                                 .arg(rangeRefText(target), text.toUpper()), 5000);
    m_view->setFocus();
}

bool CalcWindow::confirmDiscardChanges()
{
    if (!m_document->isDirty()) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("Loading the template discards unsaved changes to the current "
           "workbook. Continue?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    // A cancelled or failed save also cancels the template load.
    return answer != QMessageBox::Save || saveDocument();
}

} // namespace QindaOffice::Apps::QindaCalc
