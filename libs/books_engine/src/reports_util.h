// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal building blocks shared by the reports_*.cpp files: column
// sets, hierarchical account sections with subtotals, total rows and the
// fixed-point percentage. Not part of the app-facing API.

#include "accounts.h"
#include "report_table.h"

#include <QHash>
#include <QList>

namespace QindaOffice::Books {

class CompanyFile;

namespace ReportUtil {

// One money column: its id/title and debit−credit per account.
struct ValueColumn {
    QString id;
    QString title;
    QHash<qint64, qint64> net;
    QDate from; // for drill-down
    QDate to;
};

[[nodiscard]] QString periodText(const QDate &from, const QDate &to);
[[nodiscard]] QString basisText(Basis basis);
[[nodiscard]] ReportTable makeTable(CompanyFile &file, const QString &id, const QString &title,
                                    const QString &subtitle);
// Column 0 = label, then one Money column per value column.
void setColumns(ReportTable &table, const QString &labelTitle, const QList<ValueColumn> &columns);

[[nodiscard]] ReportRow sectionRow(const QString &key, const QString &title, int columnCount, int depth = 0);
[[nodiscard]] ReportRow moneyRow(RowKind kind, const QString &key, const QString &label,
                                 const QList<qint64> &values, int depth = 0);

// Appends the accounts of `types` as a hierarchical block (parents,
// children, "Total <parent>" subtotals) under a Section row, then a
// Subtotal row `total:<key>` labelled "Total <title>". Values are shown
// as sign × (debit − credit): sign +1 for debit-normal sections (assets,
// expenses), −1 for credit-normal ones. Accounts whose whole subtree is
// zero in every column are left out. Returns the per-column totals.
QList<qint64> appendSection(ReportTable &table, const QList<Account> &chart, const QList<AccountType> &types,
                            const QString &key, const QString &title, const QList<ValueColumn> &columns,
                            int sign, int depth = 0);

// part / whole as a percentage with `scale` decimals, half-up; 0 when
// whole is 0.
[[nodiscard]] qint64 percentOf(qint64 part, qint64 whole, int scale = 1);

// Σ element-wise.
[[nodiscard]] QList<qint64> add(const QList<qint64> &a, const QList<qint64> &b, int sign = 1);

} // namespace ReportUtil
} // namespace QindaOffice::Books
