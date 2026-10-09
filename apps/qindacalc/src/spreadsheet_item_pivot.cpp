// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem's PivotTable drawing (M74, D-083): the look of a cell in
// a pivot's output — the PivotTable style's fill, bold, text colour and
// rules, the compact form's indent (calc_engine's pivot_style.h, drawn at
// paint time like a table style, never stored in the cells) — and Excel's
// placeholder over an empty PivotTable.
//
// AGENT-NOTE: the theme's accents come from the workbook's own theme part
// (calc_io's ThemePalette), cached by the theme's bytes: the palette is
// parsed once per theme, not once per paint.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "pivot_style.h"
#include "sheet.h"
#include "xlsx_style.h"

#include <QPainter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

const QVector<Rgb> &SpreadsheetItem::pivotTheme() const
{
    const QByteArray &theme = m_document->workbook()->source.themeXml;
    if (m_pivotTheme.isEmpty() || !(m_pivotThemeKey.isSharedWith(theme) || m_pivotThemeKey == theme)) {
        m_pivotThemeKey = theme;
        const IO::ThemePalette palette = theme.isEmpty() ? IO::ThemePalette::officeDefault() : IO::themePaletteFromXml(theme);
        const auto rgb = [&palette](int index) {
            const QRgb c = palette.slot(index);
            return Rgb{quint8(qRed(c)), quint8(qGreen(c)), quint8(qBlue(c))};
        };
        // pivot_style.h's order (dk1, lt1, dk2, lt2, accents); the palette's
        // index swaps the background/text pairs (xlsx_style.h).
        m_pivotTheme = {rgb(1), rgb(0), rgb(3), rgb(2), rgb(4), rgb(5), rgb(6), rgb(7), rgb(8), rgb(9)};
        if (palette.colors.size() < 10) {
            m_pivotTheme = pivotDefaultTheme();
        }
    }
    return m_pivotTheme;
}

std::optional<PivotCellLook> SpreadsheetItem::pivotLookAt(const Sheet &sheet, const CellAddr &addr) const
{
    for (const PivotTable &pivot : sheet.pivots()) {
        if (pivot.extent.contains(addr)) {
            return pivotCellLook(pivot, addr, pivotTheme());
        }
    }
    return std::nullopt;
}

void SpreadsheetItem::paintPivotRules(QPainter &p, const QRectF &cell, const PivotCellLook &look) const
{
    if (look.top) {
        p.setPen(QPen(QColor(look.top->r, look.top->g, look.top->b), 1));
        p.drawLine(cell.topLeft(), cell.topRight());
        if (look.topDouble) {
            p.drawLine(cell.topLeft() + QPointF(0, 2), cell.topRight() + QPointF(0, 2));
        }
    }
    if (look.bottom) {
        p.setPen(QPen(QColor(look.bottom->r, look.bottom->g, look.bottom->b), 1));
        p.drawLine(cell.bottomLeft(), cell.bottomRight());
    }
}

void SpreadsheetItem::paintPivotPlaceholders(QPainter &p)
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return;
    }
    for (const PivotTable &pivot : sheet->pivots()) {
        if (!pivot.render || !pivot.render->empty) {
            continue;
        }
        const CellRange &body = pivot.render->body;
        const QRectF first = cellRect(body.topLeft);
        const QRectF last = cellRect(body.bottomRight);
        const QRectF box = QRectF(first.topLeft(), last.bottomRight()).adjusted(1, 1, -1, -1);
        // Document colours (D-004): the placeholder is part of the sheet.
        p.fillRect(box, QColor(0xf3, 0xf6, 0xfb));
        p.setPen(QPen(QColor(0x8e, 0xa9, 0xdb), 1));
        p.drawRect(box);
        QFont font = scaledFont();
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor(0x20, 0x20, 0x28));
        const QRectF text = box.adjusted(8, 8, -8, -8);
        p.drawText(text, int(Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap), pivot.name);
        font.setBold(false);
        p.setFont(font);
        p.drawText(text.adjusted(0, 24 * m_zoom, 0, 0), int(Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap),
                   tr("To build a report, choose fields from the PivotTable Field List"));
    }
}

} // namespace QindaOffice::Apps::QindaCalc
