// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A footnote's or endnote's text (M77: `kind`): new (noteId empty) inserts
// a mark at the caret; editing an existing note to empty removes note and
// mark.
Tk.Dialog {
    id: dialog
    property string noteId: ""
    property string kind: "footnote"
    property alias text: area.text
    signal footnoteApplied(string id, string text)
    signal noteApplied(string kind, string id, string text)
    objectName: "footnoteDialog"
    title: kind === "endnote" ? qsTr("Endnote") : qsTr("Footnote")
    dialogWidth: 420
    primaryText: qsTr("Save")
    onAccepted: {
        if (kind === "endnote") dialog.noteApplied(kind, noteId, area.text)
        else dialog.footnoteApplied(noteId, area.text)
    }
    onOpened: area.forceActiveFocus(Qt.TabFocusReason)
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption { text: dialog.noteId.length > 0 ? qsTr("Note text (empty removes the note)") : qsTr("Note text") }
        Tk.TextArea { id: area; objectName: "footnoteText"; width: 380; rows: 4 }
    }
}
