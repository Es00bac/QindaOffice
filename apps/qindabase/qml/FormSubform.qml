// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// A subform control's content (M89, D-097): QindaTK's FormDesigner creates
// it as its `subformComponent` in the control's frame (preview and edit
// mode) and hands over the properties declared here (QindaTK
// docs/forms-reports.md#form-controls). It shows the records of the
// subform's form (or table) whose link child field holds the form record's
// link master value, as an editable datasheet whose new records join the
// record (DatasheetModel::openLinked) — Access's subform in Datasheet view.
// FormView opens the linked sheets as it moves between records
// (openSubforms); this only shows them.
//
// AGENT-NOTE: the designer assigns `masterRecord` once, when it creates the
// frame, and the record changes under it; the master value comes from the
// form (FormView.stored) instead, so it is never stale.
Item {
    id: subform

    property var view: null
    property string formName: ""
    property string controlId: ""
    property string linkMasterFields: ""
    property string linkChildFields: ""
    readonly property int index: view !== null ? view.subformIndex(controlId) : -1
    readonly property DatasheetModel sheet: view !== null && view.base !== null && index >= 0 ? view.base.subformSheet(index) : null
    readonly property var target: view !== null && view.base !== null ? view.base.subformTable(formName) : ({ ok: false })

    objectName: "formSubform_" + controlId

    Tk.Notice {
        objectName: "formSubformProblem_" + subform.controlId
        visible: !subform.target.ok || subform.index < 0
        anchors.fill: parent
        variant: "info"
        text: subform.index < 0 ? qsTr("A form shows at most four subforms.") : String(subform.target.error || "")
    }
    Tk.EditableGrid {
        objectName: "formSubformGrid_" + subform.controlId
        visible: subform.target.ok && subform.sheet !== null
        anchors.fill: parent
        model: subform.sheet !== null ? subform.sheet.grid : null
        tooltip: subform.formName
        alternatingRows: true
        fillHandleEnabled: false
        rowHeaderWidth: Tk.Theme.size.control * 1.6
        columnLabel: function(id, index) { return subform.sheet !== null ? subform.sheet.columnLabel(id) : id }
        rowLabel: function(id, index) { return id === "new" ? "✱" : String(index + 1) }
    }
}
