// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Edit Recipient List (M77, D-086): the data source's records
// with Word's check boxes (cleared ones are left out of the merge), sort by
// a column heading, Filter and Sort, Find Duplicates and Find Recipient.
// Answered through MailMergeController (write_mailmerge.h); every change
// applies at once, as in Word.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var rows: []
    property string sortKey: ""
    property int sortOrder: Qt.AscendingOrder
    objectName: "recipientListDialog"
    title: qsTr("Mail Merge Recipients")
    subtitle: mailMerge.sourceName
    dialogWidth: 760
    primaryText: qsTr("OK")
    secondaryText: ""

    function reload() {
        const list = dialog.mailMerge.recipientRows()
        const fields = dialog.mailMerge.fields
        const out = []
        for (let r = 0; r < list.length; ++r) {
            const row = { index: list[r].index, included: list[r].included }
            for (let c = 0; c < fields.length; ++c) row["f" + c] = list[r].values[c]
            out.push(row)
        }
        rows = out
        const columns = [checkColumn]
        for (let c = 0; c < fields.length; ++c)
            columns.push(fieldColumn.createObject(table, { key: "f" + c, title: fields[c] }))
        table.columns = columns
    }

    Connections {
        target: dialog.mailMerge
        function onRecipientsRequested() { dialog.sortKey = ""; dialog.reload(); dialog.open() }
        function onChanged() { if (dialog.opened) dialog.reload() }
    }

    Component {
        id: fieldColumn
        Tk.TableColumn { width: Tk.Theme.size.tableColumn * 1.6; sortable: true; resizable: true }
    }
    Component {
        id: checkCell
        Tk.CheckBox {
            objectName: "recipientInclude"
            checked: parent && parent.row ? parent.row.included : false
            onClicked: dialog.mailMerge.setIncluded(parent.row.index, checked)
        }
    }
    Tk.TableColumn { id: checkColumn; key: "included"; title: "✓"; width: 32; sortable: false; delegate: checkCell }
    FilterSortDialog { id: filterSort; mailMerge: dialog.mailMerge }
    DuplicatesDialog { id: duplicates; mailMerge: dialog.mailMerge }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            text: qsTr("%1 of %2 records are in the merge. Clear a box to leave a recipient out; click a heading to sort.")
                  .arg(dialog.mailMerge.recipientCount).arg(dialog.rows.length)
        }
        Tk.DataTable {
            id: table
            objectName: "recipientTable"
            width: 700
            height: 300
            model: dialog.rows
            alternatingRows: true
            sortKey: dialog.sortKey
            sortOrder: dialog.sortOrder
            emptyText: qsTr("No recipients")
            tooltip: qsTr("Recipients")
            onSortRequested: function(key, order) {
                dialog.sortKey = key
                dialog.sortOrder = order
                const column = parseInt(key.substring(1))
                dialog.mailMerge.sortRecipients(dialog.mailMerge.fields[column], order === Qt.DescendingOrder)
            }
        }
        Tk.Flex {
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "recipientsAll"; text: qsTr("Select all"); onClicked: dialog.mailMerge.setAllIncluded(true) }
            Tk.Button { objectName: "recipientsNone"; text: qsTr("Clear all"); onClicked: dialog.mailMerge.setAllIncluded(false) }
            Tk.Button { objectName: "recipientsFilter"; text: qsTr("Filter and Sort…"); iconName: "funnel"; onClicked: filterSort.load() }
            Tk.Button { objectName: "recipientsDuplicates"; text: qsTr("Find Duplicates…"); iconName: "copy"; onClicked: duplicates.load() }
            Tk.Button { objectName: "recipientsFind"; text: qsTr("Find Recipient…"); iconName: "search"; onClicked: dialog.mailMerge.findRequested() }
        }
    }
}
