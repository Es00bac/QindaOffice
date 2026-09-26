// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Asks for a parameter query's values ([Start date], [Donor], …), one
// field each, with the type the engine expects; the values go back as
// {name: text} and the engine parses them (a bad date is refused there,
// with the prompt named).
Tk.Dialog {
    id: dialog

    property var parameters: []
    property var callback: null
    property var values: ({})

    title: qsTr("Enter parameter values")
    subtitle: qsTr("The query asks for these before it runs.")
    primaryText: qsTr("Run")
    secondaryText: qsTr("Cancel")

    function ask(list, done) {
        parameters = list
        values = ({})
        callback = done
        open()
    }
    onAccepted: { const f = callback; callback = null; if (f) f(values) }
    onRejected: callback = null

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: dialog.parameters
            delegate: Tk.PropertyRow {
                required property var modelData
                required property int index
                label: modelData.prompt
                hint: modelData.typeLabel
                Tk.TextField {
                    objectName: "parameter_" + index
                    placeholderText: modelData.type === "date" ? "2026-03-31" : ""
                    focus: index === 0
                    onTextChanged: {
                        const next = Object.assign({}, dialog.values)
                        next[modelData.name] = text
                        dialog.values = next
                    }
                    onAccepted: if (index === dialog.parameters.length - 1) dialog.accept()
                }
            }
        }
    }
}
