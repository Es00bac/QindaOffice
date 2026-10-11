// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc
Tk.Dialog {
    id: dialog
    required property CalcHost host
    property var fields: []
    property string error: ""
    property bool reopen: false
    objectName: "pivotCalculatedDialog"
    title: qsTr("Insert Calculated Field")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("Add / Modify")
    primaryEnabled: name.text.trim().length > 0 && formula.text.trim().length > 0
    function load() { fields = host.pivotCalculatedFields(); name.text = ""; formula.text = "="; error = "" }
    onAccepted: {
        error = host.applyPivotCalculatedField(name.text, formula.text)
        reopen = error.length > 0
    }
    onClosed: if (reopen) { reopen = false; open() }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "pivotCalculatedError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: name; objectName: "pivotCalculatedName" } }
        Tk.PropertyRow { label: qsTr("Formula"); Tk.TextField { id: formula; objectName: "pivotCalculatedFormula"; mono: true } }
        Tk.Caption { text: qsTr("Fields are summed before the formula is evaluated, including totals."); wrapMode: Text.WordWrap }
        Tk.PropertyRow {
            label: qsTr("Fields")
            Tk.ComboBox {
                id: field
                objectName: "pivotCalculatedFields"
                model: dialog.fields.map(function(f) { return f.name })
                onActivated: {
                    const f = dialog.fields[currentIndex]
                    if (f.formula.length > 0) { name.text = f.name; formula.text = f.formula }
                }
            }
        }
        Tk.Button {
            objectName: "pivotCalculatedInsert"
            text: qsTr("Insert Field")
            enabled: field.currentIndex >= 0
            onClicked: formula.text += "'" + dialog.fields[field.currentIndex].name.replace(/'/g, "''") + "'"
        }
    }
}
