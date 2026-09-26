// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotSource: the datasheet's rows as TYPED values for QindaTK's query
// engine (Tk.QueryController → Tk.QueryBuilder / Tk.PivotView), which is
// strict about types — a display string like "$1,250.00" would be text.
//
// AGENT-NOTE: the engine sums in double (QindaTK query_core: "a host
// requiring exact currency arithmetic must provide its own backend"), so
// currency crosses as units (1250.00) and pivot totals of money are exact
// only to double precision; docs/qindabase.md says so. The datasheet's own
// totals row and saved queries total in integer minor units.

#include <qindatk/editable_grid_model.h>

namespace QindaOffice::Apps::Base {

class PivotSource final : public QindaTK::GridCellSource {
public:
    using QindaTK::GridCellSource::GridCellSource;
    void setRows(const QStringList &columnIds, const QList<QVariantList> &rows);
    [[nodiscard]] QVariant value(const QString &rowId, const QString &columnId) const override;

private:
    QHash<QString, int> m_columns;
    QList<QVariantList> m_rows;
};

} // namespace QindaOffice::Apps::Base
