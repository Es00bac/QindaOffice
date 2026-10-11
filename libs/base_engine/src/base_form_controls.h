// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M80's form controls (D-089 §4), as forms saved before M89 keep them:
// subforms (a relationship's child records under the form's record),
// command buttons (Access's On Click: an embedded macro or a named one) and,
// for lookup fields, the choice of a combo or a list box. Since M89 (D-097)
// a form is laid out with QindaTK's positioned controls; base_form_layout.h
// turns these into them when such a form opens, and this file only reads
// and checks the old shape (templates still write it).
//
// AGENT-CONTRACT (with base_form_layout.cpp, which migrates them, and
// base_templates.cpp, which writes them):
//  * A form's definition keeps "controls": [{"kind":"subform", "id",
//    "caption", "table", "linkChild", "linkParent"} | {"kind":"button",
//    "id", "caption", "macro"} | {"kind":"button", "id", "caption",
//    "actions":[…]} (an embedded macro, base_macro.h)] in form order, and
//    per field in "fields" a "control": "combo" (the default) or "list" for
//    a lookup field. Unknown members of a control survive a save.
//  * A subform shows the records of `table` whose `linkChild` equals the
//    form record's `linkParent`: one of the relationships whose parent is
//    the form's table (subformChoices). Its new records get the link value.
//  * The window no longer shows these beside the designer: the form opens
//    with them laid out as positioned controls (base_form_layout.h).

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
