// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared vocabulary of the bookkeeping engine (docs/books.md, D-045):
// the Result every mutating call returns, the error codes callers switch
// on, and the enums stored in the company file.
//
// AGENT-CONTRACT (storage): every enum below is persisted as its integer
// value in the .qbooks schema (schema_migrations.cpp). Never renumber or
// reuse a value; append new ones. books_io and the QindaBooks app switch on
// them too.

#include <QString>
#include <QtGlobal>

namespace QindaOffice::Books {

enum class ErrorCode {
    None = 0,
    Invalid,           // a required field is missing or malformed
    Unbalanced,        // debits != credits
    ClosedPeriod,      // on/before the lock date without an override
    UnknownAccount,
    UnknownParty,
    UnknownItem,
    NotFound,
    CurrencyMismatch,
    OverApplied,       // applying more than an open balance
    HasPostings,       // delete refused: deactivate instead
    AlreadyVoided,
    Reconciled,        // the transaction has reconciled lines
    NotZero,           // reconciliation difference is not zero
    Duplicate,
    InactiveAccount,
    Storage,           // SQLite failed; the message carries its text
    NewerSchema,       // the file was written by a newer QindaBooks
    NotACompanyFile,
};

// The outcome of every mutating API. `id` is the row the call created or
// changed (document id for business documents) and 0 on failure.
struct Result {
    bool ok = false;
    ErrorCode code = ErrorCode::None;
    QString error;
    qint64 id = 0;

    static Result success(qint64 id = 0) { return Result{true, ErrorCode::None, {}, id}; }
    static Result failure(ErrorCode code, const QString &message)
    {
        return Result{false, code, message, 0};
    }
    explicit operator bool() const { return ok; }
};

// Reports recognise income/expense either when invoiced/billed (accrual) or
// when paid (cash). Stored in company settings as the default.
enum class Basis { Accrual = 0, Cash = 1 };

// Employees are not parties: they have their own list (employees.h, M68).
// Owner (M71, D-081): an owner or partner paid by draws or distributions —
// QuickBooks Desktop's "Other Names"; never a customer or a vendor.
enum class PartyKind { Customer = 1, Vendor = 2, Owner = 3 };

// Business documents. Every kind but Estimate, InvoiceTemplate and
// PurchaseOrder posts one balanced JournalTransaction while it is not a
// draft and not void.
enum class DocKind {
    JournalEntry = 1,
    Invoice = 2,
    SalesReceipt = 3,
    ReceivePayment = 4,
    CreditMemo = 5,
    Refund = 6,
    Estimate = 7,
    Bill = 8,
    BillPayment = 9,
    Expense = 10,         // debit card / cash
    Check = 11,
    CreditCardCharge = 12,
    VendorCredit = 13,
    Transfer = 14,
    Deposit = 15,
    SalesTaxPayment = 16,
    InvoiceTemplate = 17, // a recurring template's body; never posts
    OpeningBalance = 18,
    PurchaseOrder = 19,   // M68 (D-077): a vendor order; never posts
    Paycheck = 20,        // M71 (D-081): wages, withholding, employer taxes, net pay
    PayrollLiabilityPayment = 21, // M71: a deposit of payroll taxes or deductions
};

[[nodiscard]] QString docKindName(DocKind kind);
[[nodiscard]] bool docKindPosts(DocKind kind);
// Documents whose open balance is tracked through AR/AP (they are the
// targets or sources of applications) and that the cash basis defers.
[[nodiscard]] bool docKindIsReceivable(DocKind kind); // Invoice, CreditMemo
[[nodiscard]] bool docKindIsPayable(DocKind kind);    // Bill, VendorCredit

// Persisted document status. Derived states (partial, paid, overdue) are
// computed from applications and the due date; see DocumentStatus.
// Closed: an estimate converted, a purchase order closed by hand (M68).
enum class DocState { Draft = 0, Open = 1, Void = 2, Closed = 3 };

// Derived, never stored. PartiallyReceived (M68): a purchase order some of
// whose quantities bills have received (purchase_orders.h).
enum class DocumentStatus { Draft, Open, Partial, Paid, Overdue, Void, Pending, Closed, PartiallyReceived };
[[nodiscard]] QString documentStatusName(DocumentStatus status);

} // namespace QindaOffice::Books
