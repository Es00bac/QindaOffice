// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A form's positioned controls (M89, D-097). QindaTK's FormDesigner lays a
// form out as positioned controls (QindaTK D-311: the design's
// `form.controls`, each {id, type, name, x, y, width, height, ...} in design
// units with label, textBox, comboBox, listBox, checkBox, subform,
// commandButton, tabControl, image, line and rectangle types). A form saved
// before M89 has none: its shown fields (form order and labels; M80's combo
// or list choice for a lookup) and M80's subforms and buttons
// (base_form_controls.h) are laid out here once, in Access's stacked layout
// — a label column and a control column — when the form opens.
//
// AGENT-CONTRACT (with apps/qindabase base_service_form_layout.cpp,
// FormDesignBridge.qml and FormSubform.qml):
//  * A bound control names its field in `fieldName`. A lookup field
//    (isLookupColumn) is a comboBox or listBox whose `items` the window fills
//    from the related records each time the form opens; stored designs
//    never keep them. Labels are lookupLabel()'s, the record's key is what
//    is saved.
//  * A subform names a form in `formName` (the table it shows; a table's
//    own name is accepted too) and links one child field to one master
//    field (`linkChildFields`, `linkMasterFields`), as M80's relationship
//    subforms did.
//  * A command button's `actionId` is a macro action id (base_macro.h's
//    catalog, run with the control's `arguments` map) or "runMacro" with
//    arguments.macro naming a saved macro. M80's embedded macros of more
//    than one step keep their `actions` list beside the first action's id.
//  * Ids are stable: "<field>" and "<field>_label" for laid-out fields,
//    M80's own ids ("subform1", "button2") for its controls.

#include "base_file.h"
#include "base_records.h"

#include <QVariantMap>

namespace QindaOffice::Base {

// Design units of the stacked layout (logical pixels at 100 %, document
// geometry like a report's page, not theme chrome).
struct FormLayoutUnits {
    static constexpr int left = 8;
    static constexpr int labelWidth = 112;
    static constexpr int controlLeft = 128;
    static constexpr int controlWidth = 240;
    static constexpr int lineHeight = 24;
    static constexpr int gap = 8;
    static constexpr int listHeight = 96;
    static constexpr int subformHeight = 160;
    static constexpr int buttonWidth = 120;
    static constexpr int buttonsPerRow = 3;
};

// A lookup a combo or list box shows: a related table's display field or
// an M86 value list. Keys retain the field's storage type in both cases.
[[nodiscard]] bool isLookupColumn(const ColumnInfo &column);

// The controls for `definition` (a form object, base_objects.h) over
// `table`: its shown fields stacked in form order, then M80's subforms (a
// caption label over each) and command buttons.
[[nodiscard]] QVariantList layOutForm(const BaseFile &file, const QString &table, const QVariantMap &definition);

// A saved form whose source is `table`, "" when there is none.
[[nodiscard]] QString formOverTable(const BaseFile &file, const QString &table);

// The text a combo or list box shows for a lookup key: the related
// record's display text, with the key after it when another related record
// shows the same text ("Jane Smith (17)"), so every item names one record.
[[nodiscard]] QString lookupLabel(const BaseFile &file, const ColumnInfo &column, const QVariant &key);
// Every lookup item in display order, as lookupLabel() names them, with
// their keys (at most `limit`).
struct LookupItems {
    QStringList labels;
    QVariantList keys;
};
[[nodiscard]] LookupItems lookupItems(const BaseFile &file, const ColumnInfo &column, int limit = 1000);

// A calculated control's value (Access's "=[qty] * [price]") over one
// record whose values are as the form shows them (each parsed by its field:
// currency text, a lookup's label, masked text). The result is the text the
// datasheet would show; `error` when the expression does not compile,
// totals records, or names no field of `table`.
struct ControlValue {
    bool ok = false;
    QString text;
    QString error;
};
[[nodiscard]] ControlValue evaluateControlExpression(const BaseFile &file, const QString &table,
                                                     const QString &expression, const QVariantMap &record);

} // namespace QindaOffice::Base
