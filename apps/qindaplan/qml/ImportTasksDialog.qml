// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Import Tasks from Excel or CSV (M83, D-092) — Project's Import Wizard for
// tasks: the first sheet of a workbook or a CSV file, its first rows
// previewed, each column mapped onto a task field (the engine guesses from
// the column names: Project's own, QindaPlan's sheet titles, the field ids),
// then appended to the plan as one undo step. Values go through the same
// typing as the sheet; what a field refuses comes back as warnings.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property url source: ""
    property bool headerRow: true
    property var preview: ({})
    property var mapping: []
    readonly property var fieldIds: (preview.fields || []).map(function(f) { return f.id })
    readonly property var fieldTitles: (preview.fields || []).map(function(f) { return f.title })
    readonly property Component columnComponent: Component { Tk.TableColumn { } }

    objectName: "importTasksDialog"
    title: qsTr("Import Tasks")
    subtitle: String(source).replace(/^.*\//, "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: qsTr("Import")
    secondaryText: qsTr("Cancel")
    primaryEnabled: preview.ok === true && mapping.indexOf("name") >= 0

    function openFor(url) {
        source = url
        headerRow = true
        load()
        open()
    }
    function load() {
        preview = plan.importPreview(source, headerRow)
        mapping = preview.ok ? preview.mapping.slice() : []
        const headers = preview.headers || []
        const cols = []
        for (let i = 0; i < headers.length; ++i)
            cols.push(dialog.columnComponent.createObject(previewTable, { key: "c" + i, title: headers[i], width: Tk.Theme.size.tableColumn }))
        previewTable.columns = cols
        previewTable.model = (preview.rows || []).map(function(row) {
            const record = {}
            for (let i = 0; i < headers.length; ++i) record["c" + i] = row[i] === undefined ? "" : row[i]
            return record
        })
    }
    function setMapping(column, field) {
        const next = mapping.slice()
        // A field takes one column, as in Project's map.
        if (field !== "") for (let i = 0; i < next.length; ++i) if (next[i] === field) next[i] = ""
        next[column] = field
        mapping = next
    }
    onAccepted: {
        const r = plan.importTasks(source, mapping, headerRow)
        if (!r.ok) {
            plan.notify(qsTr("Nothing was imported"), String(r.error || ""), "danger")
            return
        }
        const warnings = r.warnings || []
        plan.notify(qsTr("Imported"), qsTr("%n task(s) added.", "", r.tasks)
                    + (warnings.length > 0 ? " " + warnings.slice(0, 3).join(" ") + (warnings.length > 3 ? " …" : "") : ""),
                    warnings.length > 0 ? "warning" : "info")
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice {
            objectName: "importTasksError"
            visible: dialog.preview.ok === false
            variant: "danger"
            title: qsTr("The file cannot be read")
            text: String(dialog.preview.error || "")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.CheckBox {
                objectName: "importTasksHeaderRow"
                text: qsTr("First row holds the column names")
                checked: dialog.headerRow
                onToggled: { dialog.headerRow = checked; dialog.load() }
            }
            Item { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("%n row(s)", "", Math.max(0, dialog.preview.count || 0)); Tk.Flex.shrink: 0 }
        }
        Tk.Caption {
            visible: dialog.preview.ok === true && dialog.mapping.indexOf("name") < 0
            wrapMode: Text.WordWrap
            text: qsTr("Map one column to Task Name — rows without a name are not imported.")
        }
        Tk.Overline { title: qsTr("MAP THE COLUMNS") }
        Repeater {
            model: dialog.preview.headers || []
            delegate: Tk.PropertyRow {
                id: mapRow
                required property var modelData
                required property int index
                objectName: "importTasksColumn_" + index
                label: String(modelData)
                Tk.ComboBox {
                    objectName: "importTasksField_" + mapRow.index
                    small: true
                    model: dialog.fieldTitles
                    currentIndex: Math.max(0, dialog.fieldIds.indexOf(dialog.mapping[mapRow.index] || ""))
                    tooltip: qsTr("The task field this column fills")
                    onActivated: function(i) { dialog.setMapping(mapRow.index, dialog.fieldIds[i]) }
                }
            }
        }
        Tk.Overline { title: qsTr("FIRST ROWS") }
        Tk.DataTable {
            id: previewTable
            objectName: "importTasksPreview"
            implicitHeight: Tk.Theme.size.row * 6
            tooltip: qsTr("The first rows of the file")
            emptyText: qsTr("No rows")
        }
    }
}
