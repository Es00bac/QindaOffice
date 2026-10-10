// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Data ▸ Link Data to Shapes (M87, D-095; Visio's Data Selector and
// Automatically Link wizard in one page): the file's first rows as they
// will be read, the worksheet of a workbook, the column that identifies a
// row (Refresh finds each shape's row again by it), and — when asked — the
// column whose value is matched against every shape's text or a data field
// so those shapes link at once. One undo step.
Tk.Dialog {
    id: dialog
    objectName: "diagramLinkDataDialog"

    required property DiagramService diagram
    property url source
    property var preview: ({ columns: [], rows: [], sheets: [] })
    property string sheet: ""
    property int keyColumn: -1
    property int matchColumn: 0
    property string shapeField: ""
    readonly property Component columnComponent: Component { Tk.TableColumn {} }

    title: qsTr("Link data to shapes")
    subtitle: String(source).replace(/^.*\//, "")
    primaryText: qsTr("Link")
    primaryEnabled: (preview.columns || []).length > 0
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4

    function openWith(url) {
        source = url
        sheet = ""
        load()
        open()
    }
    function load() {
        preview = diagram.readDataFile(source, sheet)
        sheet = preview.sheet || ""
        keyColumn = -1
        matchColumn = (preview.columns || []).length > 0 ? 0 : -1
        const cols = []
        const header = preview.columns || []
        for (let i = 0; i < header.length; ++i)
            cols.push(columnComponent.createObject(previewTable, { key: "c" + i, title: header[i], flex: 1,
                                                                   minWidth: Tk.Theme.size.tableColumn }))
        previewTable.columns = cols
        previewTable.model = (preview.rows || []).map(function(row) {
            const record = {}
            for (let i = 0; i < row.length; ++i) record["c" + i] = row[i]
            return record
        })
    }
    onAccepted: diagram.linkData(source, sheet, keyColumn, matchColumn, shapeField.trim())

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Notice {
            objectName: "linkDataError"
            visible: (dialog.preview.error || "").length > 0
            variant: "danger"
            text: dialog.preview.error || ""
        }
        Tk.PropertyRow {
            label: qsTr("Worksheet")
            visible: (dialog.preview.sheets || []).length > 1
            Tk.ComboBox {
                objectName: "linkDataSheet"
                model: dialog.preview.sheets || []
                currentIndex: Math.max(0, (dialog.preview.sheets || []).indexOf(dialog.sheet))
                tooltip: qsTr("The worksheet holding the table")
                onActivated: function(index) { dialog.sheet = dialog.preview.sheets[index]; dialog.load() }
            }
        }
        Tk.DataTable {
            id: previewTable
            objectName: "linkDataPreview"
            visible: (dialog.preview.rows || []).length > 0
            Tk.Flex.basis: Tk.Theme.size.row * (Math.min(7, (dialog.preview.rows || []).length) + 1.5)
            Tk.Flex.shrink: 0
            tooltip: qsTr("The first rows as they will be read")
        }
        Tk.Caption {
            text: qsTr("%n row(s) in all; the first row names the columns.", "", dialog.preview.rowCount || 0)
        }
        Tk.PropertyRow {
            label: qsTr("Rows are known by")
            Tk.ComboBox {
                objectName: "linkDataKey"
                model: [qsTr("Their order")].concat(dialog.preview.columns || [])
                currentIndex: dialog.keyColumn + 1
                tooltip: qsTr("The column that identifies a row when the data is refreshed")
                onActivated: function(index) { dialog.keyColumn = index - 1 }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Link shapes where")
            Tk.ComboBox {
                objectName: "linkDataMatch"
                model: [qsTr("(link none now)")].concat(dialog.preview.columns || [])
                currentIndex: dialog.matchColumn + 1
                tooltip: qsTr("The column matched against each shape")
                onActivated: function(index) { dialog.matchColumn = index - 1 }
            }
        }
        Tk.PropertyRow {
            label: qsTr("equals the shape's")
            visible: dialog.matchColumn >= 0
            Tk.TextField {
                objectName: "linkDataField"
                small: true
                text: dialog.shapeField
                placeholderText: qsTr("text (or a shape data field)")
                tooltip: qsTr("Leave empty to match the shape's text, or name a shape data field")
                onTextEdited: dialog.shapeField = text
            }
        }
    }
}
