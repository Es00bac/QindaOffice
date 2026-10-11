// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel's spellings for PivotTables (M74, D-083), shared by the reader
// (xlsx_read_pivot*) and the writer (xlsx_write_pivot*): summary functions
// as dataField subtotal="…", as subtotal items (<item t="…">) and as
// pivotField …Subtotal="1" attributes; Show Values As (ECMA showDataAs and
// Excel 2010's x14 pivotShowAs); grouping (rangePr groupBy); filter types;
// ISO dates in caches; and the extension URIs Excel files them under.

#include "pivot.h"

#include <optional>

namespace QindaOffice::Calc::IO::PivotNames {

// The x14 extensions: a pivotField's fillDownLabels, a dataField's
// pivotShowAs, the table's own x14:pivotTableDefinition.
inline constexpr char kPivotFieldExt[] = "{2946ED86-A175-432a-8AC1-64E0C546D7DE}";
inline constexpr char kDataFieldExt[] = "{E15A36E0-9728-4e99-A89B-3F7291B0FE68}";
inline constexpr char kPivotTableExt[] = "{962EF5D1-5CA2-4c93-8EF4-DBF5C05439D2}";
inline constexpr char kX14[] = "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main";
// Σ Values as an unsigned field index (a pivotArea reference's field).
inline constexpr quint32 kValuesFieldUnsigned = 4294967294u;

[[nodiscard]] QString dataSubtotal(PivotFunction function); // "sum", "count", "countNums", …
[[nodiscard]] std::optional<PivotFunction> dataSubtotalFrom(const QString &name);
[[nodiscard]] QString itemType(PivotFunction function);        // "sum", "countA", "avg", …
[[nodiscard]] QString subtotalAttribute(PivotFunction function); // "sumSubtotal", "countASubtotal", …
[[nodiscard]] QList<PivotFunction> excelSubtotalOrder();

// The ECMA showDataAs value, or empty for the ones only x14 spells.
[[nodiscard]] QString showDataAs(PivotShowAs as);
[[nodiscard]] QString pivotShowAs(PivotShowAs as); // x14, for Excel 2010's calculations
[[nodiscard]] std::optional<PivotShowAs> showAsFrom(const QString &name);

[[nodiscard]] QString groupBy(PivotGroupBy by);
[[nodiscard]] PivotGroupBy groupByFrom(const QString &name);

// A filter's type attribute (captionBeginsWith, valueGreaterThan, count,
// percent, sum, date periods); nullopt for filter types not modelled.
[[nodiscard]] QString filterType(const PivotFilter &filter);
[[nodiscard]] std::optional<PivotFilter> filterFrom(const QString &type);

// "2024-01-05T00:00:00" ↔ the 1900 serial.
[[nodiscard]] QString isoDate(double serial);
[[nodiscard]] std::optional<double> serialFromIso(const QString &text);

} // namespace QindaOffice::Calc::IO::PivotNames
