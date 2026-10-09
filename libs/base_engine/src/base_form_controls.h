// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Form controls beyond QindaTK's FormDesigner (M80, D-089 §4): subforms (a
// relationship's child records under the form's record), command buttons
// (Access's On Click: an embedded macro or a named one) and, for lookup
// fields, combo or list boxes.
//
// AGENT-CONTRACT (with apps/qindabase base_service_forms.cpp,
// FormView.qml and FormControls.qml):
//  * A form's definition keeps "controls": [{"kind":"subform", "id",
//    "caption", "table", "linkChild", "linkParent"} | {"kind":"button",
//    "id", "caption", "macro"} | {"kind":"button", "id", "caption",
//    "actions":[…]} (an embedded macro, base_macro.h)] in form order, and
//    per field in "fields" a "control": "combo" (the default) or "list" for
//    a lookup field. Unknown members of a control survive a save.
//  * A subform shows the records of `table` whose `linkChild` equals the
//    form record's `linkParent`: one of the relationships whose parent is
//    the form's table (subformChoices). Its new records get the link value.
//  * QindaTK's FormDesigner draws text fields and check boxes only, so
//    lookup fields are not in the designer's mirror (base_snapshot.h); the
//    window shows them as combo or list boxes beside it. A toolkit gap,
//    reported in D-089.

#include "base_file.h"
#include "base_macro.h"

namespace QindaOffice::Base {

constexpr int kMaxSubforms = 4;
constexpr int kMaxButtons = 12;

struct FormControl {
    QString kind; // subform | button
    QString id;   // unique in its form ("subform1", "button2")
    QString caption;
    // subform
    QString table;
    QString linkChild;  // the field in `table`
    QString linkParent; // the field in the form's table
    // button: a named macro, else the embedded actions
    QString macro;
    QList<MacroStep> actions;
    QVariantMap extra; // members this version does not know
    [[nodiscard]] QVariantMap toMap() const;
    static FormControl fromMap(const QVariantMap &map);
};

[[nodiscard]] QList<FormControl> formControls(const QVariantMap &definition);
[[nodiscard]] QVariantList controlsToList(const QList<FormControl> &controls);
// The subforms a form over `table` can show: one per relationship whose
// parent is `table` (caption = the child table's name).
[[nodiscard]] QList<FormControl> subformChoices(const BaseFile &file, const QString &table);
// Problems with a form's controls ("Subform Donations: …"); empty when
// they can be shown.
[[nodiscard]] QStringList checkFormControls(const BaseFile &file, const QString &table,
                                            const QList<FormControl> &controls);
// An id not used by `controls` yet: "<kind>1", "<kind>2", …
[[nodiscard]] QString newControlId(const QList<FormControl> &controls, const QString &kind);

} // namespace QindaOffice::Base
