// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to iif_import*.cpp: the state one IIF import shares between its
// list half (iif_import_lists.cpp) and its transaction half
// (iif_import_transactions.cpp). Names compare case-insensitively, as
// QuickBooks compares them.

#include "iif_import.h"
#include "iif_reader.h"

#include <QHash>

namespace QindaOffice::Books::IifInternal {

struct Context {
    CompanyFile &file;
    const IifImportOptions &options;
    IifImportReport &report;
    int exponent = 2;
    QHash<QString, qint64> accounts;  // lower-cased name as files write it → account id
    QHash<QString, qint64> customers; // lower-cased display name → party id
    QHash<QString, qint64> vendors;
    QHash<QString, qint64> items;     // lower-cased name → item id
    QHash<QString, qint64> classes;
    QList<qint64> hiddenAccounts;     // HIDDEN Y: deactivated once the transactions are in

    Context(CompanyFile &f, const IifImportOptions &o, IifImportReport &r);

    // 0 when unknown. Order: the caller's map, the company and the file's
    // lists, then QuickBooks' names for the system accounts.
    [[nodiscard]] qint64 account(const QString &name) const;
    [[nodiscard]] qint64 roleAccount(const QString &role) const;
    // An existing party by name, or a new one (counted) when `create`.
    qint64 customer(const QString &name, bool create);
    qint64 vendor(const QString &name, bool create);
    [[nodiscard]] qint64 anyParty(const QString &name) const; // customer first, then vendor
    qint64 klass(const QString &name);
    [[nodiscard]] qint64 item(const QString &name) const { return items.value(name.trimmed().toLower()); }
};

// The system role QuickBooks' own account of that name (or EXTRA marker,
// or type for A/R and A/P) stands for; "" for an ordinary account.
[[nodiscard]] QString systemRoleFor(const QString &name, int accountType, const QString &extra);

void importLists(Context &ctx, const QList<IifRecord> &rows);
void importTransactions(Context &ctx, const QList<IifTransaction> &transactions);
// After the transactions: HIDDEN accounts become inactive where they can.
void deactivateHidden(Context &ctx);

} // namespace QindaOffice::Books::IifInternal
