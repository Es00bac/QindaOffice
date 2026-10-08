// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Check printing (M63, D-071 item 7): a written check or a bill payment
// from a bank account, printed onto standard US VOUCHER check stock — a
// letter sheet with the check in the top third and two voucher stubs
// below, ONE check per page — through the same PagePainter contract as
// every other printed form (page_output.h, D-046), so Print, Print Preview
// and Export PDF cannot drift.
//
// AGENT-NOTE (pre-printed stock): the stock already carries the bank, the
// company, the labels ("PAY TO THE ORDER OF", "DATE", "MEMO"), the check
// number, the signature line and the MICR line. The check third therefore
// prints ONLY the values — date, payee, the amount in figures and in
// words, the payee's address (placed for a double-window envelope), the
// memo — at the positions check_renderer.cpp names. The two stubs are
// blank on the stock and print in full: payee, date, number, what the
// check pays (the bills, or the expense lines) and the check amount.
// Printing on BLANK stock (MICR toner, the bank's routing digits) is not
// supported, and there is no per-printer alignment offset yet
// (docs/books.md "Check printing").

#include "books_types.h"
#include "money.h"
#include "page_output.h"
#include "printable_document.h"

#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

struct PrintableCheck {
    QString number;          // the check number; "" when the payment has none
    QString date;            // numeric, four-digit year ("03/09/2026" in en_US)
    QString payee;           // the payee's display name
    QStringList address;     // the window-envelope block: name, then postal lines
    QString amount;          // figures for the amount box: "**1,234.56"
    QString amountWords;     // "One thousand two hundred thirty-four and 56/100"
    QString memo;            // the printed memo (the document's printed note)
    // The stubs.
    QString companyName;
    QString bankAccount;     // the account the check is drawn on
    QString total;           // "$1,234.56"
    QList<PrintableColumn> columns;
    QList<QStringList> rows; // the bills paid, or the check's lines
};

// The amount in English words as checks write it: "One thousand two
// hundred thirty-four and 56/100" — the whole units in words, then the
// minor units over 10^exponent (none for a zero-exponent currency).
// AGENT-CONTRACT (tst_books_io_checks): only the first letter is upper
// case, 21–99 are hyphenated, "and" appears only before the fraction,
// zero whole units read "Zero". A negative amount returns "" (a check is
// never negative). English only, whatever the company locale.
[[nodiscard]] QString amountInWords(const Money &amount);

// Whether `docId` prints as a check: a saved, non-void Check or Bill
// Payment drawn on a BANK account (not a credit card), for more than zero.
// `why` gets the reason when it does not.
[[nodiscard]] bool isPrintableCheck(CompanyFile &file, qint64 docId, QString *why = nullptr);
bool buildPrintableCheck(CompanyFile &file, qint64 docId, PrintableCheck *out, QString *error = nullptr);

class CheckRenderer {
public:
    // One page per check, in order (page_output.h contract).
    static void paint(QPagedPaintDevice *device, QPainter *painter, const QList<PrintableCheck> &checks);
    [[nodiscard]] static PagePainter painter(const QList<PrintableCheck> &checks);
    // Convenience: build every check and write them to one PDF on US
    // Letter (voucher stock is letter-sized whatever the locale). Fails —
    // writing nothing — when any of them is not a printable check.
    static bool writeChecksPdf(CompanyFile &file, const QList<qint64> &docIds, const QString &path,
                               QString *error = nullptr);
};

} // namespace QindaOffice::Books
