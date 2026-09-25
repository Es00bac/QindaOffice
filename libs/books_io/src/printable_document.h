// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PrintableDocument: everything an invoice, estimate, sales receipt,
// credit memo or customer statement shows on paper, already formatted —
// built once from the engine, then painted by form_renderer.h onto PDF,
// paper or the print preview (one value, one painter: they cannot drift).
//
// AGENT-NOTE: builders read the engine (Documents, Lists, Sales) and do
// all money/date formatting here, in the company's locale; the renderer
// only lays text out. Money stays Money until it becomes display text —
// no floating point touches an amount (D-045).

#include "books_types.h"
#include "money.h"

#include <QByteArray>
#include <QColor>
#include <QDate>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

struct PrintableColumn {
    QString title;
    int weight = 1;          // share of the table width (the description is wide)
    bool alignRight = false; // numbers
};

struct PrintableTotal {
    QString label;
    QString amount; // formatted
    bool emphasis = false; // Total / Balance due: bold with a rule above
};

struct PrintableDocument {
    enum class Kind { Invoice, Estimate, SalesReceipt, CreditMemo, Statement };
    Kind kind = Kind::Invoice;
    QString title; // "INVOICE"

    // Company block (from CompanySettings).
    QString companyName;
    QStringList companyLines; // address lines, then contact line
    QString taxId;
    QByteArray logo;
    QColor accent;

    // Party block.
    QString partyLabel; // "Bill to"
    QString partyName;
    QStringList partyLines;

    // Right-hand facts: ("Invoice #", "INV-1001"), ("Date", …), …
    QList<QPair<QString, QString>> meta;
    QString highlightLabel; // "Balance due"
    QString highlightAmount;

    QList<PrintableColumn> columns;
    QList<QStringList> rows; // one formatted cell per column

    QList<PrintableTotal> totals;
    // Statements: the aging strip ("Current", "$120.00") …
    QList<QPair<QString, QString>> strip;

    QString message;             // printed note to the customer
    QString paymentInstructions;
    QString footer;
    QLocale locale;
};

// Invoice / estimate / sales receipt / credit memo. Other kinds, unknown
// ids and void documents return false with `error`.
bool buildPrintableDocument(CompanyFile &file, qint64 docId, PrintableDocument *out, QString *error = nullptr);
// A customer statement for [from, to] (Sales::statement).
bool buildPrintableStatement(CompanyFile &file, qint64 customerId, const QDate &from, const QDate &to,
                             PrintableDocument *out, QString *error = nullptr);

// Formatting helpers shared with the report painter.
[[nodiscard]] QLocale companyLocale(CompanyFile &file);
[[nodiscard]] QString formatDate(const QDate &date, const QLocale &locale);

} // namespace QindaOffice::Books
