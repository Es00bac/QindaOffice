// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Project's task custom fields (M83, D-092): Text1–30, Number1–20,
// Date1–10, Flag1–20, Cost1–10, Duration1–10, Start1–10 and Finish1–10 —
// renamed with an alias, given a lookup table (optionally the only values
// allowed) or a formula (custom_formula.cpp), shown as sheet columns,
// typed, filtered and sorted (plan_views.h), and carried in Microsoft
// Project XML as ExtendedAttribute elements (mspdi_custom.cpp).
//
// AGENT-CONTRACT (storage, plan_types.h Task::custom): a value is kept in
// Microsoft Project XML's own spelling so a file's values go back byte for
// byte — Text as typed; Number a decimal ("12.5"); Flag "1"/"0"; Date,
// Start and Finish "yyyy-MM-ddTHH:mm:ss"; Cost decimal CENTS (as every MSPDI
// cost); Duration "PT8H0M0S|<DurationFormat>" (the wire duration, a bar,
// Project's format code, elapsed formats included). A field with a formula
// stores nothing: its value is computed from the task each time.
//
// AGENT-CONTRACT (identity): a field is its Project name ("Text1"); its
// MSPDI FieldID is fixed by Project (customFieldCatalog, from Project 2019's
// own files). The sheet and the app use the name as the field id.

#include "plan_edit.h"
#include "plan_types.h"

#include <QLocale>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace QindaOffice::Plan {

enum class CustomFieldKind { Text, Number, Date, Flag, Cost, Duration, Start, Finish };

struct CustomFieldInfo {
    QString field;  // "Text1"
    CustomFieldKind kind = CustomFieldKind::Text;
    int number = 1;
    qint64 fieldId = 0; // MSPDI FieldID (188743731 for Text1)
};

// Every task custom field, in Project's order (Text1 … Finish10).
[[nodiscard]] const QList<CustomFieldInfo> &customFieldCatalog();
[[nodiscard]] std::optional<CustomFieldInfo> customFieldInfo(const QString &field);
[[nodiscard]] std::optional<CustomFieldInfo> customFieldByFieldId(qint64 fieldId);
[[nodiscard]] bool isCustomField(const QString &field);
// "Text", "Number", "Date", "Flag", "Cost", "Duration", "Start", "Finish".
[[nodiscard]] QString customFieldKindName(CustomFieldKind kind);
[[nodiscard]] std::optional<CustomFieldKind> customFieldKindFromName(const QString &name);
[[nodiscard]] QStringList customFieldsOfKind(CustomFieldKind kind);

// The project's definition of the field, nullptr when it has none.
[[nodiscard]] const CustomFieldDef *customFieldDef(const Project &project, const QString &field);
// "Department" (its alias) or "Text1".
[[nodiscard]] QString customFieldTitle(const Project &project, const QString &field);
// The fields a project uses — defined (alias, lookup, formula) or holding a
// value on some task — in catalog order.
[[nodiscard]] QStringList customFieldsInUse(const Project &project);

// A task's value as the sheet shows it ("Design", "12.5", "Yes",
// "Mon 2 Mar 26", "$1,200.00", "3 days"; a formula's result, "#ERROR" when
// it fails), and as an editor starts from.
[[nodiscard]] QString customFieldText(const Project &project, int index, const QString &field,
                                      const QLocale &locale = QLocale());
[[nodiscard]] QString customFieldEditText(const Project &project, int index, const QString &field,
                                          const QLocale &locale = QLocale());
// Typing a value: checked against the field's kind and, when it restricts
// to its lookup table, against the table. Empty clears it, and so does the
// field's default (0, No, $0, 0 days: what an unset field is). Refused for
// a formula field.
EditResult setCustomField(Project &project, int index, const QString &field, const QString &text,
                          const QLocale &locale = QLocale());
// The value as a number (Number, Cost in minor units, Flag 0/1, dates and
// durations in minutes) or text, for sorting and filtering. As in Project,
// an unset Number, Cost, Flag or Duration is 0; nullopt for unset text, an
// unset date (NA) and a formula that fails.
[[nodiscard]] std::optional<QVariant> customFieldValue(const Project &project, int index, const QString &field);
// A typed value read as the field's kind would read it, in the domain of
// customFieldValue (a filter's value); nullopt when it is not one.
[[nodiscard]] std::optional<QVariant> parseCustomFieldValue(const Project &project, const QString &field,
                                                            const QString &text, const QLocale &locale = QLocale());
// The value in the wire spelling (a formula's result too) for MSPDI; empty
// when unset.
[[nodiscard]] QString customFieldWireValue(const Project &project, int index, const QString &field);

// Definitions (Project ▸ Custom Fields): rename, lookup table, formula. An
// empty alias, table and formula remove the definition.
EditResult setCustomFieldAlias(Project &project, const QString &field, const QString &alias);
EditResult setCustomFieldLookup(Project &project, const QString &field, const QList<CustomLookupEntry> &entries,
                                bool restrictToList);
EditResult setCustomFieldFormula(Project &project, const QString &field, const QString &formula);

// ---- formulas (custom_formula*.cpp, custom_formula_p.h) ----
// Project's formula language, the cheap and common part: numbers, "text",
// [Field] references (Duration, Work, Cost, % Complete, Start, Finish, the
// baseline and actual fields, Name, Priority, the custom fields by name or
// alias …), + - * / \ Mod ^ & = <> < > <= >= And Or Not and parentheses,
// and IIf, Abs, Int, Round, Len, Left, Right, Mid, UCase, LCase, Trim, Str,
// Val, Year, Month, Day, Now, Date, ProjDateDiff, ProjDurConv. Durations and
// work are minutes, dates days since 30 Dec 1899 (as in Project), costs in
// currency units.
struct FormulaResult {
    bool ok = false;
    QVariant value; // double, QString, bool or (for dates) double days
    QString error;
};
[[nodiscard]] FormulaResult evaluateFormula(const Project &project, int index, const QString &formula);
// Empty when the formula parses; else what is wrong with it.
[[nodiscard]] QString formulaProblem(const Project &project, const QString &formula);

} // namespace QindaOffice::Plan
