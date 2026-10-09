// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Match Fields (M77, D-086): which of the data source's columns
// each of Word's address fields reads (the Address Block and Greeting Line
// use them); Word's own matching is the default, a different choice is
// saved with the merge (w:odso).
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var fields: []
    property var columns: []
    property var chosen: ({})
    objectName: "matchFieldsDialog"
    title: qsTr("Match Fields")
    dialogWidth: 480
    primaryText: qsTr("OK")

    Connections {
        target: dialog.mailMerge
        function onMatchFieldsRequested() {
            const info = dialog.mailMerge.matchFieldsInfo()
            dialog.columns = [""].concat(info.columns)
            dialog.fields = info.fields
            const map = {}
            for (let i = 0; i < info.fields.length; ++i) map[info.fields[i].name] = info.fields[i].column
            dialog.chosen = map
            dialog.open()
        }
    }
    onAccepted: dialog.mailMerge.setMatchFields(chosen)

    Tk.Scroll {
        width: 440
        height: 340
        Tk.Grid {
            columns: "auto 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center
            Repeater {
                model: dialog.fields
                delegate: Tk.Label { required property var modelData; text: modelData.name; Tk.Grid.column: 0; Tk.Grid.row: index; required property int index }
            }
            Repeater {
                model: dialog.fields
                delegate: Tk.ComboBox {
                    required property var modelData
                    required property int index
                    objectName: "matchField" + index
                    Tk.Grid.column: 1
                    Tk.Grid.row: index
                    small: true
                    model: dialog.columns.map(function(c) { return c.length > 0 ? c : qsTr("(not matched)") })
                    currentIndex: Math.max(0, dialog.columns.indexOf(modelData.column))
                    onActivated: function(i) {
                        const map = Object.assign({}, dialog.chosen)
                        map[modelData.name] = dialog.columns[i]
                        dialog.chosen = map
                    }
                }
            }
        }
    }
}
