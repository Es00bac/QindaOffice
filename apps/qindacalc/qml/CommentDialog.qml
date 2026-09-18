// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The comment on one cell (Shift+F2): a text area; an emptied comment is
// removed by the host.
Tk.Dialog {
    id: dialog

    property string cell: ""
    property alias text: area.text
    signal commentApplied(string text)

    objectName: "commentDialog"
    title: qsTr("Comment on %1").arg(cell)
    dialogWidth: 420
    primaryText: qsTr("Save")
    onAccepted: dialog.commentApplied(area.text)
    onOpened: area.forceActiveFocus(Qt.TabFocusReason)

    Tk.TextArea { id: area; objectName: "commentText"; width: parent.width; rows: 5; placeholderText: qsTr("Your note on this cell") }
}
