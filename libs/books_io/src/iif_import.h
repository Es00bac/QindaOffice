// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Moving from QuickBooks Desktop: importing an IIF file into a company
// (M63, D-071). Lists first — ACCNT (QuickBooks account types mapped to
// ours, sub-accounts as "Parent:Child", QuickBooks' own A/R, A/P,
// Undeposited Funds, Sales Tax Payable, Retained Earnings and Opening Bal
// Equity mapped onto this company's system accounts), CUST, VEND, INVITEM,
// CLASS — then every TRNS/SPL/ENDTRNS transaction.
//
// AGENT-CONTRACT (the ledger matches the file): a transaction is refused,
// with its line numbers, when its rows do not sum to zero, a date or
// amount does not read, or an account is neither in the company, nor in
// the file's lists, nor mapped by the caller. Every other transaction
// posts EXACTLY its rows. INVOICE, CREDIT MEMO, CASH SALE, PAYMENT, BILL,
// BILL PMT, CHECK, CREDIT CARD, DEPOSIT and TRANSFER become the matching
// QindaBooks document when that document's posting reproduces the rows
// line for line (checked after posting, inside a savepoint); otherwise —
// and for GENERAL JOURNAL and every other type — the rows post as a journal
// entry. A transaction already in the books (same date and the same
// account/debit/credit/name rows, counted per occurrence as the bank feed
// does) is skipped, so importing a file twice adds nothing.
//
// AGENT-CONTRACT (one batch): the whole import runs in ONE SQL transaction.
// options.commit == false is a dry run — the very same work, rolled back —
// so the preview's counts are what the import will do. A commit with
// refused transactions happens only when options.skipRefused is set.
// Payments, credit memos and bill payments are applied to the oldest open
// invoices / bills of the same customer or vendor when options.autoApply
// is set (IIF carries no links; QuickBooks' own IIF import leaves them
// unapplied).

#include "books_types.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

struct IifImportOptions {
    bool commit = false;      // false = dry run
    bool skipRefused = false; // commit even when some transactions are refused
    bool autoApply = true;
    // QuickBooks account name (as the file writes it) → an account of
    // this company, for names the company and the file's lists lack.
    QHash<QString, qint64> accountMap;
};

struct IifListCount {
    int added = 0;
    int existing = 0; // already in the company (by name), nothing changed
    int skipped = 0;  // not imported (see notes)
};

struct IifTypeCount {
    QString type;        // as the file spells it ("INVOICE")
    int count = 0;       // transactions of this type in the file
    int documents = 0;   // posted as the matching document
    int journal = 0;     // posted as a journal entry
    int duplicates = 0;  // already in the books
    int refused = 0;
};

struct IifRefusal {
    int firstLine = 0;
    int lastLine = 0;
    QString type;
    QString reason;
};

struct IifUnmappedAccount {
    QString name;
    QList<int> lines;
};

struct IifImportReport {
    bool ok = false;        // the file read; with commit, the batch committed
    bool committed = false;
    QString error;          // why nothing could be done (not an IIF file, …)
    IifListCount accounts;
    IifListCount customers;
    IifListCount vendors;
    IifListCount items;
    IifListCount classes;
    QList<IifTypeCount> transactions; // in the order types first appear
    QList<IifRefusal> refused;
    QList<IifUnmappedAccount> unmappedAccounts;
    QStringList notes;      // what was approximated or left out, once each
    int posted = 0;         // transactions posted (documents + journal entries)
    qint64 postedDebits = 0; // Σ debits of the posted transactions, minor units
    void note(const QString &text)
    {
        if (!notes.contains(text)) {
            notes << text;
        }
    }
};

class IifImport {
public:
    [[nodiscard]] static IifImportReport run(CompanyFile &file, const QByteArray &iif,
                                             const IifImportOptions &options = {});
    // QuickBooks ACCNTTYPE keyword → engine AccountType (as int); -1 for
    // NONPOSTING and unknown keywords. The inverse of
    // LedgerExport::iifAccountType for every type it writes.
    [[nodiscard]] static int accountTypeFromIif(const QString &keyword);
};

} // namespace QindaOffice::Books
