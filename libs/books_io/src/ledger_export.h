// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Handing the books to someone else: the general journal and the name
// lists as CSV, QuickBooks IIF (lists + journal entries) for accountants
// who work in QuickBooks Desktop, and CSV import of the lists.
//
// AGENT-CONTRACT (CSV list columns): listCsv() writes exactly the headers
// importCsv() prefers, so export → import into another company file is a
// round trip (tst_books_io_export). Import also accepts common synonyms
// ("Display Name", "E-mail", "Sales Price", QuickBooks' own headers).
//
// AGENT-NOTE (IIF): IIF is QuickBooks Desktop's tab-separated import
// format. Journal entries are exported as TRNSTYPE GENERAL JOURNAL with
// signed amounts (+ debit / − credit) — every posting becomes a balanced
// entry an accountant can import; document structure (invoice lines,
// applications) does not travel. Voided transactions are left out. The
// output imports back through IifImport (iif_import.h, M63): lists equal,
// every balance equal (tst_books_io_iif).

#include "books_types.h"

#include <QByteArray>
#include <QDate>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

enum class ListKind { Accounts, Customers, Vendors, Items };

struct IifOptions {
    bool lists = true;   // !ACCNT, !CUST, !VEND, !INVITEM
    bool journal = true; // !TRNS/!SPL/!ENDTRNS
    QDate from;          // journal range; invalid = unbounded
    QDate to;
};

struct ImportReport {
    int added = 0;
    int skipped = 0;          // already present (matched by name)
    QStringList errors;       // "row 4: unknown account type \"Assets\""
};

class LedgerExport {
public:
    // Date, Type, Num, Name, Account, Debit, Credit, Memo — one row per
    // posting line of every live transaction in [from, to].
    [[nodiscard]] static QByteArray journalCsv(CompanyFile &file, const QDate &from = {}, const QDate &to = {});
    [[nodiscard]] static QByteArray listCsv(CompanyFile &file, ListKind kind);
    [[nodiscard]] static QByteArray iif(CompanyFile &file, const IifOptions &options = {});
    // IIF account type keyword for an engine account type (BANK, AR, …).
    [[nodiscard]] static QString iifAccountType(int accountType);
};

class ListImport {
public:
    // Creates the rows through ChartOfAccounts / Lists, so validation and
    // the audit log apply. Rows whose name already exists are skipped.
    [[nodiscard]] static ImportReport importCsv(CompanyFile &file, ListKind kind, const QByteArray &csv);
};

} // namespace QindaOffice::Books
