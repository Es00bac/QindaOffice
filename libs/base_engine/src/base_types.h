// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaBase field types and value conversion (M49, D-049).
//
// AGENT-CONTRACT (storage, D-049 §2 — every reader of a .qbase file relies
// on it, including QindaTK's report engine through the snapshot, and other
// SQLite tools):
//   type        declared type   stored as
//   autoNumber  INTEGER         INTEGER PRIMARY KEY AUTOINCREMENT (rowid)
//   text        TEXT            TEXT, at most 255 characters
//   longText    LONGTEXT        TEXT
//   integer     INTEGER         INTEGER (64-bit)
//   number      REAL            REAL (binary double — use currency for money)
//   currency    CURRENCY        INTEGER minor units (cents for 2 decimals)
//   yesNo       BOOLEAN         INTEGER 0/1
//   date        DATE            TEXT yyyy-MM-dd
//   dateTime    DATETIME        TEXT yyyy-MM-dd HH:mm:ss
//   file        FILE            BLOB, at most 32 MB (base_file_field.h, M65)
//   hyperlink   HYPERLINK TEXT  TEXT: an address or Access's "display#address#…"
//                               (base_hyperlink.h, M86)
//   largeNumber BIGINT          INTEGER (64-bit; Access's Large Number, M86)
// A calculated field (M86, base_calculated.h) is a generated column of its
// result type: FieldDef::calculated, never a FieldType of its own.
// The declared type is the source of truth for a column's QindaBase type;
// qb_fields.type only disambiguates (autoNumber). Foreign SQLite files map
// by affinity (typeFromDeclared); BLOB columns are `other` (read-only).
//
// Currency never passes through floating point: parseInput reads the
// decimal text digit by digit into minor units, and display formats minor
// units with integer arithmetic.

#include "base_sqlite.h"

#include <QString>
#include <QVariant>

#include <optional>

namespace QindaOffice::Base {

// AGENT-NOTE: values are never stored as numbers (typeId is the stored
// form), so M86 inserted Hyperlink and LargeNumber before Other.
enum class FieldType {
    AutoNumber,
    Text,
    LongText,
    Integer,
    Number,
    Currency,
    YesNo,
    Date,
    DateTime,
    File,
    Hyperlink,   // M86
    LargeNumber, // M86
    Other
};

struct CurrencyFormat {
    QString symbol = QStringLiteral("$");
    int decimals = 2; // 0..4
};

constexpr int kShortTextLimit = 255;

[[nodiscard]] QString typeId(FieldType type);
[[nodiscard]] std::optional<FieldType> typeFromId(const QString &id);
[[nodiscard]] QString typeLabel(FieldType type);
[[nodiscard]] QList<FieldType> designableTypes(); // every type but Other
[[nodiscard]] QString declaredType(FieldType type);
// Affinity mapping for any declared type; `rowidPrimaryKey` = the column is
// an INTEGER PRIMARY KEY (an AutoNumber when the table says AUTOINCREMENT
// or when `autoIncrement` is known).
[[nodiscard]] FieldType typeFromDeclared(const QString &declared, bool rowidPrimaryKey = false);
[[nodiscard]] bool isNumeric(FieldType type); // integer/number/currency/autoNumber/largeNumber
// Whole numbers: AutoNumber, Whole number and (M86) Large Number.
[[nodiscard]] bool isWholeNumber(FieldType type);
// Typed in as text (every type but autoNumber, file and other).
[[nodiscard]] bool isTypable(FieldType type);

struct Parsed {
    bool ok = true;
    QVariant value; // storage value; invalid = NULL
    QString error;  // "needs a number", … (without the field name)
};

// User text → storage value. Empty text is NULL. A file field takes no
// text (Records::setFile writes it). Accepts: grouping commas,
// currency symbols and (parentheses) for negative money, yes/no/true/false/
// 1/0, ISO dates and US month/day/year dates, date-times with optional
// seconds and AM/PM.
[[nodiscard]] Parsed parseInput(FieldType type, const QString &text, const CurrencyFormat &currency = {});
// Any QVariant (a QML number or bool, or text) → storage value; a
// QByteArray is a file field's bytes.
[[nodiscard]] Parsed parseValue(FieldType type, const QVariant &value, const CurrencyFormat &currency = {});
// Storage value → display text ("$1,250.00", "Yes", "2026-03-01",
// "PNG image · 24 KB"; M86: a hyperlink's display text).
[[nodiscard]] QString displayText(FieldType type, const QVariant &stored, const CurrencyFormat &currency = {});
// Storage value → the text an editor starts from ("1250.00", "Yes").
[[nodiscard]] QString editText(FieldType type, const QVariant &stored, const CurrencyFormat &currency = {});

// Minor units ↔ text, exact.
[[nodiscard]] std::optional<qint64> parseMoney(const QString &text, int decimals, QString *error = nullptr);
[[nodiscard]] QString formatMoney(qint64 minor, int decimals, const QString &symbol, bool grouping = true);
// A value as a plain number for sorting/aggregation display (currency in units).
[[nodiscard]] QString formatNumber(double value);

} // namespace QindaOffice::Base
