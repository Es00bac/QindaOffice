// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "base_macro.h"

namespace QindaOffice::Base {

// M97: expression values for window-scoped macros. Draft record fields use
// the form's displayed/input spelling; temporary values are window-owned.
// This reads a single bound row, never changes the database, prompts for
// parameters, executes subqueries or runs arbitrary SQL.
struct MacroValue {
    bool ok = false;
    QVariant value;
    QVariant input;
    FieldType type = FieldType::Text;
    QString text;
    QString error;
};
[[nodiscard]] MacroValue evaluateMacroValue(const BaseFile &file, const QString &table,
                                             const QString &expression, const QVariantMap &record,
                                             const QVariantMap &temporaryValues,
                                             const QString &targetField = {});
// Checks balanced If/Else/EndIf and expression syntax before execution.
[[nodiscard]] QStringList checkMacroFlow(const Macro &macro);

} // namespace QindaOffice::Base
