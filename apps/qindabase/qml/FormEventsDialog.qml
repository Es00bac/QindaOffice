// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Dialog {
    id: dialog
    required property var window
    property var form: null
    property var draft: ({})
    property var targets: []
    property string targetId: ""
    property string notice: ""
    readonly property var macros: [qsTr("(none)")].concat(Array.from(window.base.macroNames || []))
    readonly property var formEvents: [
        { id: "open", label: qsTr("On Open") }, { id: "current", label: qsTr("On Current") },
        { id: "beforeUpdate", label: qsTr("Before Update") }, { id: "afterUpdate", label: qsTr("After Update") },
        { id: "close", label: qsTr("On Close") }]
    readonly property var controlEvents: [
        { id: "change", label: qsTr("On Change") }, { id: "click", label: qsTr("On Click (button)") },
        { id: "beforeUpdate", label: qsTr("Before Record Update") }, { id: "afterUpdate", label: qsTr("After Record Update") }]
    objectName: "baseFormEventsDialog"
    title: qsTr("Form Events")
    subtitle: form !== null ? form.formName : ""
    primaryText: qsTr("Apply")
    primaryEnabled: form !== null && form.designDocument !== null
    function openFor(view) {
        form = view
        draft = JSON.parse(JSON.stringify(view.designDocument.form.events || ({})))
        targets = [{ id: "", label: qsTr("Form") }].concat((view.bridge.presented.controls || []).map(function(c) {
            return { id: String(c.id), label: String(c.name || c.caption || c.id) }
        }))
        targetId = ""
        target.currentIndex = 0
        notice = ""
        open()
    }
    function selected(event) {
        const scope = targetId.length ? ((draft.controls || ({}))[targetId] || ({})) : (draft.form || ({}))
        return String(scope[event] || "")
    }
    function choose(event, macro) {
        const next = JSON.parse(JSON.stringify(draft))
        let scope
        if (targetId.length) {
            if (!next.controls) next.controls = ({})
            if (!next.controls[targetId]) next.controls[targetId] = ({})
            scope = next.controls[targetId]
        } else {
            if (!next.form) next.form = ({})
            scope = next.form
        }
        if (macro.length) scope[event] = macro
        else delete scope[event]
        draft = next
    }
    onAccepted: {
        const r = form.setEventBindings(draft)
        if (!r.ok) { notice = r.error; open() }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            text: qsTr("Choose a saved macro for each event. Create macros in Create ▸ Macro. Before Update can use CancelEvent to reject a record; SetValue changes the draft before it is saved.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.Notice { visible: dialog.notice.length > 0; variant: "warning"; text: dialog.notice }
        Tk.PropertyRow {
            label: qsTr("Form or control")
            Tk.ComboBox {
                id: target
                objectName: "baseFormEventTarget"
                model: dialog.targets
                textRole: "label"
                onActivated: function(index) { dialog.targetId = dialog.targets[index].id }
            }
        }
        Repeater {
            model: dialog.targetId.length ? dialog.controlEvents : dialog.formEvents
            delegate: Tk.PropertyRow {
                required property var modelData
                label: modelData.label
                Tk.ComboBox {
                    objectName: "baseFormEvent_" + modelData.id
                    model: dialog.macros
                    currentIndex: Math.max(0, dialog.macros.indexOf(dialog.selected(modelData.id)))
                    tooltip: modelData.label
                    onActivated: function(index) { dialog.choose(modelData.id, index > 0 ? dialog.macros[index] : "") }
                }
            }
        }
        Tk.Caption {
            text: qsTr("Bindings are part of the form design: Apply is one Undo step, then save the form. Events do not run in Design or Preview mode.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
}
