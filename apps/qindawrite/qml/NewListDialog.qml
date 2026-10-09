// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk

// Mailings ▸ Select Recipients ▸ Type a New List (M77, D-086): Word's New
// Address List — the entries under Word's columns (Customize Columns adds
// and removes them), New Entry, Delete Entry; OK asks where to save the
// list (a CSV file, which Word opens as a text data source) and makes it
// the merge's recipients (MailMergeController::saveNewList).
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var fields: []
    property var entries: []
    property int current: -1
    objectName: "newListDialog"
    title: qsTr("New Address List")
    dialogWidth: 760
    primaryText: qsTr("OK")

    function rowsModel() {
        const out = []
        for (let r = 0; r < entries.length; ++r) {
            const row = {}
            for (let c = 0; c < fields.length; ++c) row["f" + c] = entries[r][c] || ""
            out.push(row)
        }
        return out
    }
    function rebuildColumns() {
        const columns = []
        for (let c = 0; c < fields.length; ++c)
            columns.push(columnComponent.createObject(table, { key: "f" + c, title: fields[c] }))
        table.columns = columns
    }
    function setValue(column, text) {
        if (current < 0) return
        const copy = entries.slice()
        const row = copy[current].slice()
        row[column] = text
        copy[current] = row
        entries = copy
    }
    function newEntry() {
        const row = []
        for (let c = 0; c < fields.length; ++c) row.push("")
        entries = entries.concat([row])
        current = entries.length - 1
        table.currentIndex = current
    }

    Connections {
        target: dialog.mailMerge
        function onNewListRequested() {
            dialog.fields = dialog.mailMerge.newListFields()
            dialog.entries = []
            dialog.rebuildColumns()
            dialog.newEntry()
            dialog.open()
        }
    }
    onAccepted: saveDialog.open()

    Component {
        id: columnComponent
        Tk.TableColumn { width: Tk.Theme.size.tableColumn * 1.4; sortable: false; resizable: true }
    }
    Dialogs.FileDialog {
        id: saveDialog
        title: qsTr("Save Address List")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("Address list (*.csv)")]
        onAccepted: dialog.mailMerge.saveNewList(selectedFile, dialog.fields, dialog.entries)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.DataTable {
            id: table
            objectName: "newListTable"
            width: 700
            height: 160
            model: dialog.rowsModel()
            alternatingRows: true
            emptyText: qsTr("No entries")
            tooltip: qsTr("Entries")
            onActivated: function(index) { dialog.current = index }
            onCurrentIndexChanged: dialog.current = currentIndex
        }
        Tk.Flex {
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "newListEntry"; text: qsTr("New Entry"); iconName: "plus"; onClicked: dialog.newEntry() }
            Tk.Button {
                objectName: "newListDelete"
                text: qsTr("Delete Entry")
                iconName: "trash-2"
                enabled: dialog.current >= 0
                onClicked: {
                    const copy = dialog.entries.slice()
                    copy.splice(dialog.current, 1)
                    dialog.entries = copy
                    dialog.current = Math.min(dialog.current, copy.length - 1)
                }
            }
            Tk.TextField { id: columnName; objectName: "newListColumnName"; small: true; implicitWidth: 140; placeholderText: qsTr("New column") }
            Tk.Button {
                objectName: "newListAddColumn"
                text: qsTr("Add Column")
                enabled: columnName.text.trim().length > 0 && dialog.fields.indexOf(columnName.text.trim()) < 0
                onClicked: {
                    dialog.fields = dialog.fields.concat([columnName.text.trim()])
                    dialog.entries = dialog.entries.map(function(row) { return row.concat([""]) })
                    columnName.text = ""
                    dialog.rebuildColumns()
                }
            }
            Tk.Button {
                objectName: "newListRemoveColumn"
                text: qsTr("Remove Column")
                enabled: dialog.fields.length > 1
                onClicked: {
                    const at = dialog.fields.length - 1
                    dialog.fields = dialog.fields.slice(0, at)
                    dialog.entries = dialog.entries.map(function(row) { return row.slice(0, at) })
                    dialog.rebuildColumns()
                }
            }
        }
        Tk.Overline { text: dialog.current >= 0 ? qsTr("Entry %1").arg(dialog.current + 1) : qsTr("No entry") }
        Tk.Grid {
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            Repeater {
                model: dialog.fields
                delegate: Tk.PropertyRow {
                    required property string modelData
                    required property int index
                    label: modelData
                    dense: true
                    Tk.TextField {
                        objectName: "newListField" + index
                        small: true
                        enabled: dialog.current >= 0
                        text: dialog.current >= 0 && dialog.current < dialog.entries.length
                              ? (dialog.entries[dialog.current][index] || "") : ""
                        onEditingFinished: dialog.setValue(index, text)
                        onAccepted: dialog.setValue(index, text)
                    }
                }
            }
        }
    }
}
