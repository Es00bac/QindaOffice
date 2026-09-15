// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cell.h"

namespace QindaOffice::Calc {

// One sparse worksheet (plan section 7.2). Geometry bounds follow the Excel
// grid (1048576 rows x 16384 columns); the hash keeps empty space free.
class Sheet {
public:
    static constexpr qint32 kMaxRows = 1048576;
    static constexpr qint32 kMaxCols = 16384;

    explicit Sheet(QString name = {});

    [[nodiscard]] const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    [[nodiscard]] const QHash<quint64, Cell> &cells() const { return m_cells; }
    [[nodiscard]] Cell *cellAt(const CellAddr &addr);
    [[nodiscard]] const Cell *cellAt(const CellAddr &addr) const;
    // Stores the cell; returns a pointer valid until the next mutation.
    Cell &ensureCell(const CellAddr &addr);
    bool clearCell(const CellAddr &addr);

    [[nodiscard]] qreal columnWidth(qint32 col) const;  // px, 96 dpi
    [[nodiscard]] qreal rowHeight(qint32 row) const;    // px, 96 dpi
    void setColumnWidth(qint32 col, qreal px);
    void setRowHeight(qint32 row, qreal px);

    [[nodiscard]] const QList<CellRange> &merges() const { return m_merges; }
    void addMerge(const CellRange &range) { m_merges.append(range); }
    void clearMerges() { m_merges.clear(); }

    [[nodiscard]] qint32 styleIdAt(const CellAddr &addr) const;
    void setStyleIdAt(const CellAddr &addr, qint32 id);

private:
    QString m_name;
    QHash<quint64, Cell> m_cells;
    QHash<qint32, qreal> m_colWidths;
    QHash<qint32, qreal> m_rowHeights;
    QHash<quint64, qint32> m_styleIds;
    QList<CellRange> m_merges;
};

} // namespace QindaOffice::Calc
