// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The current slide's speaker notes, under the slide as in PowerPoint's
// Normal view (M67, D-076). What is typed here is the slide's notes: one
// undo step per stretch of typing (NotesEditCommand), saved into the
// slide's notes slide, shown by the presenter view and printed on the PDF's
// notes pages.
Tk.Flex {
    id: pane
    required property ShowHost showHost
    objectName: "notesPane"
    direction: Tk.Flex.Column
    gap: 0
    Tk.Scroll {
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.TextArea {
            id: notes
            objectName: "slideNotes"
            width: parent.width
            rows: 3
            placeholderText: qsTr("Click to add notes")
            text: pane.showHost.notes
            onTextChanged: if (activeFocus) pane.showHost.notes = text
        }
    }
}
