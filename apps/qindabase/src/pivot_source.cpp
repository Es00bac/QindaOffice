// SPDX-License-Identifier: GPL-3.0-or-later
#include "pivot_source.h"

namespace QindaOffice::Apps::Base {

void PivotSource::setRows(const QStringList &columnIds, const QList<QVariantList> &rows)
{
    m_columns.clear();
    for (int i = 0; i < columnIds.size(); ++i) {
        m_columns.insert(columnIds.at(i), i);
    }
    m_rows = rows;
}

QVariant PivotSource::value(const QString &rowId, const QString &columnId) const
{
    // Row ids are the grid's generated "r:N" (1-based).
    const int row = rowId.mid(2).toInt() - 1;
    const int column = m_columns.value(columnId, -1);
    if (row < 0 || row >= m_rows.size() || column < 0) {
        return {};
    }
    return m_rows.at(row).value(column);
}

} // namespace QindaOffice::Apps::Base
