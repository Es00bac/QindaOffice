// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Conditional-format rules as the dialogs see them (M78, D-087): every
// field of a CondFormatRule as a QVariantMap the QML edits — "range" (the
// sqref, "A1:A10 C1:C10"), "op" (CondFormatRule::Op), the operands and
// text, the Top 10 / average / date settings, "thresholds"
// [{type, value, gte}], "colors" ["#rrggbb"], the icon set, the data bar's
// settings, the format ("background", "foreground", "bold", "italic",
// "underline", "strikeout") and "stopIfTrue" — plus a one-line
// "description" as Excel's Rules Manager words it. Pure functions: the
// tests and the host share them.

#include "sheet_rules.h"

#include <QVariantMap>

namespace QindaOffice::Apps::QindaCalc {

namespace CalcEng = QindaOffice::Calc;

[[nodiscard]] QVariantMap condRuleToMap(const CalcEng::CondFormatRule &rule);
// The rule a map describes; fields the map leaves out keep `base`'s
// (an edited rule keeps its priority, dxf and x14 ties). An invalid range
// gives a rule whose range is invalid.
[[nodiscard]] CalcEng::CondFormatRule condRuleFromMap(const QVariantMap &map, const CalcEng::CondFormatRule &base = {});
[[nodiscard]] QString condRuleDescription(const CalcEng::CondFormatRule &rule);

// "A1:A10 C1:C10" ↔ the rule's ranges (the first is its anchor).
[[nodiscard]] QString sqrefText(const QList<CalcEng::CellRange> &ranges);
[[nodiscard]] QList<CalcEng::CellRange> sqrefFromText(const QString &text);

// The rules a quick command makes over `range` (Excel's gallery defaults):
// a blue gradient data bar, the Green - Yellow - Red colour scale, the
// 3 Traffic Lights icon set.
[[nodiscard]] CalcEng::CondFormatRule defaultDataBar(const CalcEng::CellRange &range);
[[nodiscard]] CalcEng::CondFormatRule defaultColorScale(const CalcEng::CellRange &range);
[[nodiscard]] CalcEng::CondFormatRule defaultIconSet(const CalcEng::CellRange &range);

// Rules with `cleared` taken out of their ranges (Clear Rules from Selected
// Cells): a rule wholly inside goes, one partly inside keeps the rest of
// its cells, its formulas moved to the new anchor so they still mean what
// they meant.
[[nodiscard]] QList<CalcEng::CondFormatRule> rulesWithout(const QList<CalcEng::CondFormatRule> &rules,
                                                          const CalcEng::CellRange &cleared);

} // namespace QindaOffice::Apps::QindaCalc
