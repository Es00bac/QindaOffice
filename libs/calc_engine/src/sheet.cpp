// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet.h"

namespace QindaOffice::Calc {

namespace {
constexpr qreal kDefaultColWidth = 96.0;  // 64 pt char column ~= 96 px at 96 dpi (plan: width units)
constexpr qreal kDefaultRowHeight = 20.0; // 15 pt ~= 20 px
} // namespace

Sheet::Sheet(QString name)
    : m_name(std::move(name))
{
}

Cell *Sheet::cellAt(const CellAddr &addr)
{
    return const_cast<Cell *>(std::as_const(*this).cellAt(addr));
}

const Cell *Sheet::cellAt(const CellAddr &addr) const
{
    const auto it = m_cells.constFind(addr.key());
    return it == m_cells.constEnd() ? nullptr : &it.value();
}

Cell &Sheet::ensureCell(const CellAddr &addr)
{
    return m_cells[addr.key()];
}

bool Sheet::clearCell(const CellAddr &addr)
{
    m_styleIds.remove(addr.key());
    return m_cells.remove(addr.key()) > 0;
}

qreal Sheet::columnWidth(qint32 col) const
{
    return m_colWidths.value(col, kDefaultColWidth);
}

qreal Sheet::rowHeight(qint32 row) const
{
    return m_rowHeights.value(row, kDefaultRowHeight);
}

void Sheet::setColumnWidth(qint32 col, qreal px)
{
    m_colWidths.insert(col, px);
}

void Sheet::setRowHeight(qint32 row, qreal px)
{
    m_rowHeights.insert(row, px);
}

qint32 Sheet::styleIdAt(const CellAddr &addr) const
{
    return m_styleIds.value(addr.key(), 0);
}

void Sheet::setStyleIdAt(const CellAddr &addr, qint32 id)
{
    m_styleIds.insert(addr.key(), id);
}

} // namespace QindaOffice::Calc
