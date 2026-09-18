// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A footnote's text: new (noteId empty) inserts a marker at the caret;
// editing an existing note to empty removes note and marker.
Tk.Dialog {
    id: dialog
    property string noteId: ""
    property alias text: area.text
    signal footnoteApplied(string id, string text)
    objectName: "footnoteDialog"
    title: qsTr("Footnote")
    dialogWidth: 420
    primaryText: qsTr("Save")
    onAccepted: dialog.footnoteApplied(noteId, area.text)
    onOpened: area.forceActiveFocus(Qt.TabFocusReason)
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption { text: dialog.noteId.length > 0 ? qsTr("Note text (empty removes the note)") : qsTr("Note text") }
        Tk.TextArea { id: area; objectName: "footnoteText"; width: 380; rows: 4 }
    }
}
