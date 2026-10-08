// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Value model for calc_engine (plan section 7.2): the sparse cell payload,
// cell/range addressing and the shared coercion rules every evaluator path
// must apply identically.

#include <QSharedData>
#include <QString>
#include <QVariant>
#include <QVector>
#include <optional>
#include <variant>

namespace QindaOffice::Calc {

// Colour as plain RGB so calc_engine stays Qt-Core-only (the view layer maps
// it to QColor).
struct Rgb {
    quint8 r = 0;
    quint8 g = 0;
    quint8 b = 0;
    bool operator==(const Rgb &) const = default;
};

enum class Err {
    Null,   // empty intersection
    Div0,
    Value,  // wrong operand type
    Ref,    // bad reference
    Name,   // unknown name/function
    Num,    // invalid numeric value
    NA,     // not available
    Circular,
};

// Display/export text for an error value. Err:522 mirrors LibreOffice;
// #REF! is the storage/export spelling (plan section 7.2).
[[nodiscard]] QString errText(Err err);
[[nodiscard]] Err errFromExport(const QString &text);

using Value = std::variant<std::monostate, double, QString, bool, Err>;

[[nodiscard]] bool isBlank(const Value &v);
[[nodiscard]] bool isErr(const Value &v);
[[nodiscard]] bool isNumber(const Value &v);
[[nodiscard]] bool isText(const Value &v);
[[nodiscard]] bool isBool(const Value &v);

// Coercions (plan section 7.2): numeric text converts in arithmetic, other
// text -> #VALUE!, bool -> 1/0, empty -> 0. These return std::nullopt on
// failure so callers can propagate Err::Value.
[[nodiscard]] std::optional<double> toNumber(const Value &v);
[[nodiscard]] std::optional<bool> toBool(const Value &v);   // FALSE/TRUE, numbers != 0
[[nodiscard]] QString toText(const Value &v);               // display text

// Total order for comparisons: number < text < bool (plan section 7.2).
[[nodiscard]] int compareValues(const Value &a, const Value &b);

// 0-based cell address. row/col are qint32 so 32-bit shift packing stays
// exact; the workbook bounds usable space far below 2^31.
struct CellAddr {
    qint32 row = -1;
    qint32 col = -1;

    [[nodiscard]] bool isValid() const { return row >= 0 && col >= 0; }
    bool operator==(const CellAddr &) const = default;

    // Stable hash key for QHash<quint64, Cell> (col in the high dword so
    // whole-column iteration order reads naturally).
    [[nodiscard]] quint64 key() const
    {
        return (quint64(quint32(col)) << 32) | quint64(quint32(row));
    }
};

struct CellRange {
    CellAddr topLeft;
    CellAddr bottomRight; // inclusive

    [[nodiscard]] bool isValid() const
    {
        return topLeft.isValid() && bottomRight.isValid() && topLeft.row <= bottomRight.row
            && topLeft.col <= bottomRight.col;
    }
    [[nodiscard]] qint64 cellCount() const
    {
        return isValid() ? qint64(bottomRight.row - topLeft.row + 1)
                               * qint64(bottomRight.col - topLeft.col + 1)
                         : 0;
    }
    [[nodiscard]] bool contains(const CellAddr &addr) const
    {
        return addr.row >= topLeft.row && addr.row <= bottomRight.row
            && addr.col >= topLeft.col && addr.col <= bottomRight.col;
    }
    bool operator==(const CellRange &) const = default;
};

// A parsed reference: an optional sheet qualifier (empty = current sheet),
// a cell or range, absolute flags per axis, whole-row/column forms.
struct RangeRef {
    QString sheet;
    CellRange range;
    bool absRow0 = false;
    bool absCol0 = false;
    bool absRow1 = false;
    bool absCol1 = false;
    bool wholeRows = false;
    bool wholeCols = false;

    [[nodiscard]] bool isSingleCell() const
    {
        return !wholeRows && !wholeCols && range.topLeft == range.bottomRight;
    }
    bool operator==(const RangeRef &) const = default;
};

// Human/canonical text forms ("A1", "$B$2", "A1:B3", "'My Sheet'!A1").
[[nodiscard]] QString addrText(const CellAddr &addr, bool absRow = false, bool absCol = false);
[[nodiscard]] QString colName(qint32 col); // 0 -> "A", 25 -> "Z", 26 -> "AA"
[[nodiscard]] std::optional<qint32> colFromName(const QString &name);
[[nodiscard]] QString rangeRefText(const RangeRef &ref);
// The name to compare against Workbook::sheetIndex: RangeRef.sheet keeps the
// file's display spelling, quotes included ("'Data'!A1" → "'Data'"), so a
// comparison without this never matches (eval.cpp's resolveSheet unquotes
// the same way).
[[nodiscard]] QString unquotedSheetName(QString name);
// True when a sheet name has to be quoted in a reference ('Q1 Sales'!A1):
// anything but a plain ASCII identifier, and identifiers that read as a cell
// or R1C1 address ("AB12", "R1C1") or a boolean. Quoting a name that did not
// need it is always valid in Excel, so the rule errs towards quoting (M61).
[[nodiscard]] bool sheetNameNeedsQuotes(const QString &name);
// The spelling a reference uses for a sheet: quoted, with ' doubled, when
// sheetNameNeedsQuotes says so; unchanged otherwise. A name that is already
// in quotes (RangeRef.sheet keeps the file's spelling) comes back as is —
// Excel forbids a sheet name that starts or ends with an apostrophe, so the
// quotes cannot be part of a real name.
[[nodiscard]] QString quotedSheetName(const QString &name);

} // namespace QindaOffice::Calc
