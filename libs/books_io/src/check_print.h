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
// supported. M68 (D-077): every page can be shifted by the printer's
// alignment offset (check_alignment.h), and an alignment test page shows
// where the fields land (docs/books.md "Check printing").

#include "books_types.h"
#include "check_alignment.h"
#include "money.h"
#include "page_output.h"
#include "printable_document.h"

#include <QDate>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

// M71 (D-081): a paycheck's stubs are a pay stub — earnings and deductions
// beside the taxes withheld, this check and the year to date, net pay
// (pay_stub.cpp). The SSN prints masked to its last four digits.
struct PayStubLine {
    QString label;
    QString current;
    QString yearToDate;
};

struct PrintablePayStub {
    QString employee;
    QString maskedSsn;       // "•••-••-6789" or ""
    QString period;          // "Sep 26 – Oct 9, 2026"
    QString payDate;
    QString rate;            // "$60,000.00 a year" / "80 h at $25.00"
    QList<PayStubLine> earnings; // gross pay and the deductions before tax
    QList<PayStubLine> taxes;    // withheld
    QList<PayStubLine> after;    // deductions after tax
    PayStubLine net;
    QStringList notes;
};

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
    std::optional<PrintablePayStub> payStub; // M71: a paycheck's stubs
};

// The amount in English words as checks write it: "One thousand two
// hundred thirty-four and 56/100" — the whole units in words, then the
// minor units over 10^exponent (none for a zero-exponent currency).
// AGENT-CONTRACT (tst_books_io_checks): only the first letter is upper
// case, 21–99 are hyphenated, "and" appears only before the fraction,
// zero whole units read "Zero". A negative amount returns "" (a check is
// never negative). English only, whatever the company locale.
[[nodiscard]] QString amountInWords(const Money &amount);

// Whether `docId` prints as a check: a saved, non-void Check, Bill Payment
// or (M71) Paycheck drawn on a BANK account (not a credit card), for more
// than zero.
// `why` gets the reason when it does not.
[[nodiscard]] bool isPrintableCheck(CompanyFile &file, qint64 docId, QString *why = nullptr);
bool buildPrintableCheck(CompanyFile &file, qint64 docId, PrintableCheck *out, QString *error = nullptr);

class CheckRenderer {
public:
    // One page per check, in order (page_output.h contract). `offset`
    // (M68) moves everything on the sheet — check third and stubs.
    static void paint(QPagedPaintDevice *device, QPainter *painter, const QList<PrintableCheck> &checks,
                      const CheckOffset &offset = {});
    [[nodiscard]] static PagePainter painter(const QList<PrintableCheck> &checks, const CheckOffset &offset = {});
    // Convenience: build every check and write them to one PDF on US
    // Letter (voucher stock is letter-sized whatever the locale). Fails —
    // writing nothing — when any of them is not a printable check.
    static bool writeChecksPdf(CompanyFile &file, const QList<qint64> &docIds, const QString &path,
                               QString *error = nullptr, const CheckOffset &offset = {});

    // M68: the alignment test page, printed on plain paper and held over a
    // sheet of check stock against the light — a sample check (`sample`;
    // empty fields get sample values) with a crosshair at each field's
    // anchor, shifted by `offset`, and how to read it in the stubs.
    // `printer` and `alignmentText` ("0.05 in right, 0.10 in down") are
    // printed in the instructions.
    static void paintAlignmentTest(QPagedPaintDevice *device, QPainter *painter, const CheckOffset &offset,
                                   const QString &printer, const QString &alignmentText,
                                   const PrintableCheck &sample = {});
    [[nodiscard]] static PagePainter alignmentTestPainter(const CheckOffset &offset, const QString &printer,
                                                          const QString &alignmentText, const PrintableCheck &sample = {});
    // A sample check for the test page: the company, today, $1,234.56.
    [[nodiscard]] static PrintableCheck sampleCheck(CompanyFile &file, const QDate &date);
};

} // namespace QindaOffice::Books
