// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Insert Merge Field (M77, D-086): the data source's fields
// (Database Fields) or Word's address fields as Match Fields maps them
// (Address Fields); Insert puts a MERGEFIELD at the caret and the dialog
// stays open for the next one, as in Word.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var addressFields: []
    readonly property var shown: database.checked ? mailMerge.fields : addressFields.map(function(f) { return f.name })
    property int chosen: 0
    objectName: "insertMergeFieldDialog"
    title: qsTr("Insert Merge Field")
    dialogWidth: 380
    primaryText: qsTr("Insert")
    secondaryText: qsTr("Close")

    function insertChosen() {
        if (dialog.shown.length === 0) return
        if (database.checked) {
            dialog.mailMerge.insertField(dialog.shown[dialog.chosen])
        } else {
            const column = addressFields[dialog.chosen].column
            if (column.length > 0) dialog.mailMerge.insertField(column)
        }
    }
    Connections {
        target: dialog.mailMerge
        function onInsertFieldRequested() {
            dialog.addressFields = dialog.mailMerge.matchFieldsInfo().fields
            dialog.chosen = 0
            database.checked = true
            dialog.open()
        }
    }
    onAccepted: { insertChosen(); open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            gap: Tk.Theme.space.md
            Tk.RadioButton { id: address; objectName: "mergeFieldAddress"; text: qsTr("Address Fields"); onClicked: { database.checked = false; dialog.chosen = 0 } }
            Tk.RadioButton { id: database; objectName: "mergeFieldDatabase"; text: qsTr("Database Fields"); checked: true; onClicked: { address.checked = false; dialog.chosen = 0 } }
        }
        Tk.Scroll {
            width: 340
            height: 220
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.shown
                    delegate: Tk.ListRow {
                        required property string modelData
                        required property int index
                        objectName: "mergeFieldRow" + index
                        text: modelData
                        selected: dialog.chosen === index
                        onClicked: dialog.chosen = index
                        onDoubleClicked: { dialog.chosen = index; dialog.insertChosen() }
                    }
                }
            }
        }
    }
}
