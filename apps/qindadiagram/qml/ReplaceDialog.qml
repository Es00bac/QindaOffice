// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Edit ▸ Replace (D-050): replace text in every shape and connector label
// of the current page, as one undo step.
Tk.Dialog {
    id: dialog
    objectName: "diagramReplaceDialog"

    required property DiagramService diagram

    title: qsTr("Replace")
    subtitle: qsTr("In every shape and label on this page")
    primaryText: qsTr("Replace all")
    primaryEnabled: findField.text.length > 0

    function openWith(text) {
        findField.text = text || ""
        replaceField.text = ""
        open()
        findField.forceActiveFocus()
    }
    onAccepted: diagram.replace(findField.text, replaceField.text)

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Find")
            Tk.TextField { id: findField; objectName: "replaceFind"; tooltip: qsTr("Text to find") }
        }
        Tk.PropertyRow {
            label: qsTr("Replace with")
            Tk.TextField { id: replaceField; objectName: "replaceWith"; tooltip: qsTr("Replacement text") }
        }
    }
}
