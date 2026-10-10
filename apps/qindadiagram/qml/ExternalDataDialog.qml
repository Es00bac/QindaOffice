// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Data ▸ External Data (M87, D-095; Visio's External Data window): a
// recordset's rows with how many shapes link to each; a row links to the
// selected shapes, the whole page links automatically by a column, rows
// become new linked shapes, and Refresh reads the file again. Every action
// is one undo step.
Tk.Dialog {
    id: dialog
    objectName: "diagramExternalDataDialog"

    required property DiagramService diagram
    property string recordsetId: ""
    readonly property var recordset: {
        const list = diagram.recordsets
        for (let i = 0; i < list.length; ++i) if (list[i].id === recordsetId) return list[i]
        return list.length > 0 ? list[0] : null
    }
    readonly property var columns: recordset !== null ? recordset.columns : []
    property int matchColumn: 0
    property int textColumn: 0
    readonly property Component columnComponent: Component { Tk.TableColumn {} }

    title: qsTr("External data")
    subtitle: recordset !== null ? qsTr("%1 · refreshed %2").arg(recordset.source).arg(recordset.refreshed) : ""
    primaryText: qsTr("Done")
    secondaryText: ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6

    function openFor() {
        recordsetId = diagram.recordsets.length > 0 ? diagram.recordsets[0].id : ""
        fill()
        open()
    }
    // The rows as a Tk.DataTable: a column per data column, then "Linked".
    function fill() {
        const cols = []
        for (let i = 0; i < columns.length; ++i)
            cols.push(columnComponent.createObject(rows, { key: "c" + i, title: columns[i], flex: 1,
                                                           minWidth: Tk.Theme.size.tableColumn }))
        cols.push(columnComponent.createObject(rows, { key: "linked", title: qsTr("Linked"), width: Tk.Theme.size.control * 3 }))
        rows.columns = cols
        rows.model = recordset === null ? [] : diagram.recordsetRows(recordset.id).map(function(r) {
            const record = { linked: r.linked > 0 ? qsTr("%n shape(s)", "", r.linked) : "" }
            for (let i = 0; i < r.cells.length; ++i) record["c" + i] = r.cells[i]
            return record
        })
    }
    Connections {
        target: dialog.diagram
        function onChanged() { if (dialog.visible) dialog.fill() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.PropertyRow {
            label: qsTr("Data")
            Tk.ComboBox {
                objectName: "externalDataRecordset"
                model: dialog.diagram.recordsets.map(function(r) { return r.name })
                currentIndex: Math.max(0, dialog.diagram.recordsets.findIndex(function(r) { return r.id === (dialog.recordset || {}).id }))
                tooltip: qsTr("Which linked table to show")
                onActivated: function(index) { dialog.recordsetId = dialog.diagram.recordsets[index].id; dialog.fill() }
            }
        }
        Tk.DataTable {
            id: rows
            objectName: "externalDataRows"
            Tk.Flex.basis: Tk.Theme.size.row * 9
            Tk.Flex.shrink: 0
            emptyText: qsTr("Link a workbook or CSV file with Data ▸ Link Data to Shapes.")
            tooltip: qsTr("Rows of the data; pick one to link it to the selected shapes")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "externalDataLinkRow"
                small: true
                iconName: "link"
                text: qsTr("Link row to selection")
                enabled: dialog.recordset !== null && rows.currentIndex >= 0 && dialog.diagram.selection.length > 0
                onClicked: dialog.diagram.linkSelectionToRow(dialog.recordset.id, rows.currentIndex)
            }
            Tk.Button {
                objectName: "externalDataCreate"
                small: true
                iconName: "shapes"
                text: rows.currentIndex >= 0 ? qsTr("Shape from row") : qsTr("Shapes from all rows")
                enabled: dialog.recordset !== null
                onClicked: {
                    const all = []
                    for (let i = 0; i < dialog.recordset.rowCount; ++i) all.push(i)
                    dialog.diagram.shapesFromRows(dialog.recordset.id, rows.currentIndex >= 0 ? [rows.currentIndex] : all,
                                                  dialog.diagram.quickShape || "process", dialog.textColumn)
                }
            }
            Tk.Button {
                objectName: "externalDataRefresh"
                small: true
                iconName: "refresh-cw"
                text: qsTr("Refresh")
                enabled: dialog.recordset !== null
                onClicked: dialog.diagram.refreshData(dialog.recordset.id)
            }
            Tk.Button {
                objectName: "externalDataRemove"
                small: true
                iconName: "trash-2"
                text: qsTr("Remove data")
                enabled: dialog.recordset !== null
                onClicked: { dialog.diagram.removeRecordset(dialog.recordset.id); dialog.recordsetId = ""; dialog.fill() }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Link every shape whose text is in") }
            Tk.ComboBox {
                objectName: "externalDataMatch"
                small: true
                model: dialog.columns
                currentIndex: dialog.matchColumn
                tooltip: qsTr("The column matched against the shapes' text")
                onActivated: function(index) { dialog.matchColumn = index }
            }
            Tk.Button {
                objectName: "externalDataAutoLink"
                small: true
                iconName: "wand-sparkles"
                text: qsTr("Automatically link")
                enabled: dialog.recordset !== null
                onClicked: dialog.diagram.autoLinkData(dialog.recordset.id, dialog.matchColumn, "")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Shapes made from rows show") }
            Tk.ComboBox {
                objectName: "externalDataTextColumn"
                small: true
                model: dialog.columns
                currentIndex: dialog.textColumn
                tooltip: qsTr("The column new shapes take their text from")
                onActivated: function(index) { dialog.textColumn = index }
            }
        }
    }
}
