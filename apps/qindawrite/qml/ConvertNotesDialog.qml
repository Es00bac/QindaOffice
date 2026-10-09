// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// References ▸ Convert Notes (M77, D-086): Word's Convert Notes — every
// footnote to an endnote, every endnote to a footnote, or swap the two; one
// undo step (NotesController::convertNotes).
Tk.Dialog {
    id: dialog
    required property QtObject notes
    property int mode: 0
    readonly property var modes: ["toEndnotes", "toFootnotes", "swap"]
    objectName: "convertNotesDialog"
    title: qsTr("Convert Notes")
    dialogWidth: 380
    primaryText: qsTr("OK")

    Connections {
        target: dialog.notes
        function onConvertRequested() { dialog.mode = 0; dialog.open() }
    }
    onAccepted: dialog.notes.convertNotes(modes[mode])

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.RadioButton { objectName: "convertToEndnotes"; text: qsTr("Convert all footnotes to endnotes"); checked: dialog.mode === 0; onClicked: dialog.mode = 0 }
        Tk.RadioButton { objectName: "convertToFootnotes"; text: qsTr("Convert all endnotes to footnotes"); checked: dialog.mode === 1; onClicked: dialog.mode = 1 }
        Tk.RadioButton { objectName: "convertSwap"; text: qsTr("Swap footnotes and endnotes"); checked: dialog.mode === 2; onClicked: dialog.mode = 2 }
    }
}
