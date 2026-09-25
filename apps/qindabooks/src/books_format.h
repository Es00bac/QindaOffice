// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QML boundary of QindaBooks: how engine values become QVariant maps
// and how typed text comes back (D-047). Used only by BooksService and
// BooksHost; the engine never sees a QVariant.
//
// AGENT-CONTRACT (money across QML, docs/books.md "QindaBooks (app)"):
//  * every amount leaves C++ as INTEGER minor units (`amount`) plus the
//    formatted text (`amountText`); QML never does money arithmetic;
//  * amounts coming IN are either text the user typed (a string, parsed
//    with the company locale, then the C locale) or minor units (a
//    number, e.g. an `amount` read back from document()). A string with
//    more fraction digits than the currency has is refused, never rounded;
//  * the only floating-point values are the dashboard's chart series,
//    named `…Chart`, in MAJOR units — never fed back into the books.

#include "books_types.h"
#include "documents.h"
#include "report_table.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace QindaOffice::Apps::Books {

namespace Engine = ::QindaOffice::Books;

// Money display/parsing for one company (its currency and locale).
struct MoneyFormat {
    QString currency = QStringLiteral("USD");
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedStates);

    [[nodiscard]] QString text(qint64 minor) const;  // "$1,234.50"
    [[nodiscard]] QString plain(qint64 minor) const; // "1234.50"
    // Minor units in MAJOR units for a chart axis (the one double).
    [[nodiscard]] double chart(qint64 minor) const;
    // Text (locale, then C locale) or a number of minor units. Empty text
    // is zero. nullopt with `error` filled when it is not an amount.
    [[nodiscard]] std::optional<qint64> parse(const QVariant &value, QString *error = nullptr) const;
    [[nodiscard]] Engine::Money money(qint64 minor) const { return Engine::Money(minor, currency); }
};

namespace Format {

[[nodiscard]] QString isoDate(const QDate &date);
// "2026-03-31" (or a QDate/QDateTime variant); invalid when empty/bad.
[[nodiscard]] QDate date(const QVariant &value);
[[nodiscard]] qint64 id(const QVariant &value);

// Document kinds as the QML API names them: invoice, estimate,
// salesReceipt, payment, creditMemo, refund, bill, billPayment, expense,
// check, creditCardCharge, vendorCredit, journalEntry, transfer, deposit,
// salesTaxPayment, recurringTemplate, openingBalance.
[[nodiscard]] QString kindKey(Engine::DocKind kind);
[[nodiscard]] std::optional<Engine::DocKind> kindFromKey(const QString &key);
[[nodiscard]] QString kindLabel(Engine::DocKind kind);
// draft, open, partial, paid, overdue, void, pending, closed.
[[nodiscard]] QString statusKey(Engine::DocumentStatus status);
[[nodiscard]] QString statusLabel(Engine::DocumentStatus status);

// {ok, id, error, code} from an engine Result.
[[nodiscard]] QVariantMap result(const Engine::Result &result);
[[nodiscard]] QVariantMap failure(const QString &error);
[[nodiscard]] QVariantMap success(qint64 id = 0);

// The ReportTable as QML renders it (report_table.h; the cells keep their
// integer `value` and carry display `text`).
[[nodiscard]] QVariantMap report(const Engine::ReportTable &table, const MoneyFormat &money);
[[nodiscard]] QVariantMap drill(const Engine::DrillDown &drill);
[[nodiscard]] Engine::DrillDown drillFrom(const QVariantMap &map);

// A Decimal as the user typed it ("1.5"); a percent Decimal (a fraction,
// 0.1) as "10".
[[nodiscard]] QString percentText(const Engine::Decimal &fraction);

} // namespace Format
} // namespace QindaOffice::Apps::Books
