// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Form & Report ▸ Add Subform / Add Button (M80, D-089 §4): a subform from
// one of the relationships whose parent is the form's table, or a command
// button that runs a saved macro or one action (Access's Command Button
// Wizard). The form saves the new control as one undo step.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var form: null // the FormView it adds to
    property string kind: "subform"
    property var choices: []
    property int choice: 0
    property string caption: ""
    property string runs: "action" // action | macro
    property string macroName: ""
    property var step: ({ action: "openForm", arguments: ({}) })

    title: kind === "subform" ? qsTr("Add a subform") : qsTr("Add a command button")
    subtitle: kind === "subform" ? qsTr("The records related to this one, through one relationship.")
                                 : qsTr("A button that runs a macro or one action when it is clicked.")
    primaryText: qsTr("Add")
    primaryEnabled: kind === "subform" ? choices.length > 0
                                       : caption.trim().length > 0 && (runs === "action" || macroName.length > 0)

    function openFor(newKind, formView) {
        kind = newKind
        form = formView
        const c = base.formControls(formView.formName)
        choices = c.ok ? c.subformChoices : []
        choice = 0
        caption = kind === "subform" && choices.length > 0 ? choices[0].caption : ""
        runs = "action"
        macroName = base.macroNames.length > 0 ? base.macroNames[0] : ""
        step = { action: "openForm", arguments: ({}) }
        open()
    }
    onAccepted: {
        let control
        if (kind === "subform") {
            control = Object.assign({}, choices[choice], { kind: "subform", id: "", caption: caption.trim() || choices[choice].caption })
        } else if (runs === "macro") {
            control = { kind: "button", id: "", caption: caption.trim(), macro: macroName, actions: [] }
        } else {
            control = { kind: "button", id: "", caption: caption.trim(), macro: "", actions: [step] }
        }
        form.addControl(control)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice {
            visible: dialog.kind === "subform" && dialog.choices.length === 0
            variant: "info"
            text: qsTr("No other table points to this one. Add a relationship first (Database Tools ▸ Relationships).")
        }
        Tk.PropertyRow {
            visible: dialog.kind === "subform" && dialog.choices.length > 0
            label: qsTr("Shows")
            Tk.ComboBox {
                objectName: "controlSubformTable"
                model: dialog.choices.map(function(c) { return qsTr("%1 (by %2)").arg(c.table).arg(c.linkChild) })
                currentIndex: dialog.choice
                onActivated: function(i) { dialog.choice = i; dialog.caption = dialog.choices[i].caption }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Caption")
            Tk.TextField {
                objectName: "controlCaption"
                text: dialog.caption
                placeholderText: dialog.kind === "subform" ? qsTr("Donations") : qsTr("New donation")
                onTextChanged: dialog.caption = text
            }
        }
        Tk.Segmented {
            objectName: "controlRuns"
            visible: dialog.kind === "button"
            small: true
            model: [{ text: qsTr("Do one action"), iconName: "play" }, { text: qsTr("Run a macro"), iconName: "workflow" }]
            currentIndex: dialog.runs === "macro" ? 1 : 0
            onActivated: function(i) { dialog.runs = i === 1 ? "macro" : "action" }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "button" && dialog.runs === "macro"
            label: qsTr("Macro")
            Tk.ComboBox {
                objectName: "controlMacro"
                model: dialog.base !== null ? dialog.base.macroNames : []
                currentIndex: Math.max(0, model.indexOf(dialog.macroName))
                onActivated: function(i) { dialog.macroName = model[i] }
            }
        }
        MacroActionRow {
            objectName: "controlAction"
            visible: dialog.kind === "button" && dialog.runs === "action"
            compact: true
            base: dialog.base
            step: dialog.step
            onEdited: function(next) { dialog.step = next }
        }
    }
}
