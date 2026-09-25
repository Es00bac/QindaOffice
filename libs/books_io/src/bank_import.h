// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bank and credit-card statement import: CSV (auto-detected or explicitly
// mapped), OFX/QFX (SGML 1.x and XML 2.x) and QIF, each producing the
// engine's BankLine list. Feed the result to Banking::importLines, which
// deduplicates (FITID, else date+amount+payee+occurrence).
//
// AGENT-CONTRACT (sign, banking.h): BankLine::amount > 0 is money INTO
// the account (a deposit; a payment or refund onto a card), < 0 money out
// (a withdrawal; a card charge). OFX and QIF already use the account
// holder's sign. CSV exports that list card charges as positive numbers
// need CsvMapping::invertSign.
//
// AGENT-GUARD: amounts are parsed from text straight into minor units of
// the company currency (`exponent` = CurrencyInfo::exponent). Never go
// through double; more decimals than the currency has (and not zeros) is a
// row error, not a silent rounding.

#include "banking.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Books {

struct CsvMapping {
    QChar delimiter;          // null = detect
    bool hasHeader = true;
    int dateColumn = -1;
    int amountColumn = -1;    // one signed amount …
    int debitColumn = -1;     // … or money out / money in columns
    int creditColumn = -1;
    int typeColumn = -1;      // "debit"/"credit" marker for unsigned amounts
    int payeeColumn = -1;
    int memoColumn = -1;
    int checkColumn = -1;
    int fitidColumn = -1;
    QString dateFormat;       // Qt format; empty = detect from the rows
    bool dayFirst = false;    // tie-break when every date fits both orders
    bool decimalComma = false;
    bool invertSign = false;  // card exports with charges as positive
    [[nodiscard]] bool isValid() const
    {
        return dateColumn >= 0 && (amountColumn >= 0 || debitColumn >= 0 || creditColumn >= 0);
    }
};

struct BankParseError {
    int row = 0; // 1-based line/record number in the file
    QString message;
};

struct BankParseResult {
    QString format; // "csv", "ofx", "qif"
    QList<BankLine> lines;
    QList<BankParseError> errors;
    QString accountId;       // OFX ACCTID
    QString currency;        // OFX CURDEF
    bool creditCard = false; // OFX CCSTMTRS / QIF !Type:CCard
    std::optional<qint64> ledgerBalance; // OFX LEDGERBAL/BALAMT, minor units
    QDate ledgerBalanceDate;
    CsvMapping mapping;      // CSV: the mapping actually used
};

namespace BankImport {

[[nodiscard]] CsvMapping detectCsvMapping(const QByteArray &data, bool dayFirst = false);
[[nodiscard]] BankParseResult parseCsv(const QByteArray &data, const CsvMapping &mapping, int exponent);
[[nodiscard]] BankParseResult parseCsv(const QByteArray &data, int exponent, bool dayFirst = false);
[[nodiscard]] BankParseResult parseOfx(const QByteArray &data, int exponent);
[[nodiscard]] BankParseResult parseQif(const QByteArray &data, int exponent, bool dayFirst = false);
// By content (OFX header / "<OFX>", QIF "!Type:"), else by suffix, else CSV.
[[nodiscard]] BankParseResult parseFile(const QString &path, int exponent, bool dayFirst = false,
                                        QString *error = nullptr);

// Exact amount text → minor units: "1,234.56", "(12.00)", "-€3,50",
// "1.234,56" (decimalComma), "12.5-". nullopt when not a number or when it
// carries more non-zero decimals than `exponent`.
[[nodiscard]] std::optional<qint64> parseAmount(const QString &text, int exponent, bool decimalComma = false);
// The first candidate format that parses every sample; empty when none.
[[nodiscard]] QString detectDateFormat(const QStringList &samples, bool dayFirst = false);
[[nodiscard]] QDate parseDate(const QString &text, const QString &format);

} // namespace BankImport
} // namespace QindaOffice::Books
