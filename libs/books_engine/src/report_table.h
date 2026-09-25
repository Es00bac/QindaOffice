// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ReportTable: the one value every report returns (docs/books.md
// "Reports"). The app renders it in a table view, books_io writes it to
// PDF and CSV — neither re-derives a number.
//
// AGENT-CONTRACT (shape):
//  * columns: typed (text, money, number, percent, date); column 0 is the
//    row label for grouped reports.
//  * rows: kind (Section header, Account/Detail line, Subtotal, Total,
//    Spacer) + depth for indentation + one cell per column + a drill-down
//    key the app turns into "show the transactions behind this number".
//  * Cells hold integers only: money in minor units, numbers/percent as
//    fixed-point `value` with `scale` decimals (percent 12.5% = value 125,
//    scale 1). No floating point anywhere (D-045).

#include "money.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

enum class ColumnType { Text, Money, Number, Percent, Date };

struct ReportColumn {
    QString id;    // stable key ("label", "total", "2026-01", "prior")
    QString title; // header text
    ColumnType type = ColumnType::Money;
};

enum class RowKind { Section, Account, Detail, Subtotal, Total, Spacer };

struct ReportCell {
    ColumnType type = ColumnType::Text;
    bool empty = true;
    QString text;
    qint64 value = 0; // money minor units, or fixed-point number
    int scale = 0;    // decimals of a Number/Percent value
    QDate date;

    [[nodiscard]] static ReportCell ofText(const QString &text);
    [[nodiscard]] static ReportCell ofMoney(qint64 minor);
    [[nodiscard]] static ReportCell ofNumber(qint64 value, int scale = 0);
    [[nodiscard]] static ReportCell ofPercent(qint64 value, int scale = 1);
    [[nodiscard]] static ReportCell ofDate(const QDate &date);
    [[nodiscard]] static ReportCell blank() { return {}; }
    // Display text: money via Money::format(locale), numbers with their
    // decimals, dates ISO. `currency` is the table's.
    [[nodiscard]] QString display(const QString &currency, const QLocale &locale = QLocale::c()) const;
};

// What a double-click on the row opens.
struct DrillDown {
    qint64 accountId = 0;
    qint64 partyId = 0;
    qint64 itemId = 0;
    QDate from;
    QDate to;
    QList<qint64> documentIds;
    [[nodiscard]] bool isEmpty() const
    {
        return accountId == 0 && partyId == 0 && itemId == 0 && documentIds.isEmpty();
    }
};

struct ReportRow {
    RowKind kind = RowKind::Account;
    int depth = 0;
    QString key; // stable: "account:12", "total:income", "party:4", "doc:88"
    QList<ReportCell> cells;
    DrillDown drill;
};

struct ReportTable {
    QString id;       // "profit_and_loss", "balance_sheet", …
    QString title;    // "Profit and Loss"
    QString company;  // display name
    QString subtitle; // "January 1 – March 31, 2026 · Accrual basis"
    QString currency;
    QList<ReportColumn> columns;
    QList<ReportRow> rows;
    QStringList notes;

    [[nodiscard]] int columnIndex(const QString &columnId) const;
    [[nodiscard]] const ReportRow *row(const QString &key) const;
    // Money cell of row `key` in column `columnId`; 0 when absent.
    [[nodiscard]] qint64 money(const QString &key, const QString &columnId) const;
    [[nodiscard]] QString text(const QString &key, const QString &columnId) const;
};

} // namespace QindaOffice::Books
