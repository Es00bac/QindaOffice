// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Import a text/CSV file or one sheet of an Excel workbook (M65) into a new
// or an existing table. The engine proposes: for CSV the delimiter, for
// Excel the sheet (the first holding data); whether the first row holds the
// names; and each column's type — from the text for CSV (codes with leading
// zeros stay text; money needs a currency sign or a money word in the
// header), from the cells themselves for Excel (base_xlsx_import.h). The
// user adjusts names, types and which columns come in, with the first rows
// previewed; the import is all-or-nothing unless "skip rows that don't
// fit" is on, and is one undo step.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property url source: ""
    property string format: "csv" // csv | xlsx
    property var proposal: ({})
    property var plan: ({ columns: [] })
    property string delimiter: ""
    property string sheet: ""
    property bool headerRow: true
    property bool intoExisting: false
    readonly property bool excel: format === "xlsx"
    readonly property var types: base !== null ? base.fieldTypes.filter(function(t) { return t.id !== "autoNumber" && t.id !== "file" }) : []
    readonly property Component columnComponent: Component { Tk.TableColumn { } }
    readonly property var delimiters: [{ id: "", label: qsTr("Detect") }, { id: ",", label: qsTr("Comma") },
                                       { id: ";", label: qsTr("Semicolon") }, { id: "tab", label: qsTr("Tab") },
                                       { id: "|", label: qsTr("Bar |") }]

    title: excel ? qsTr("Import an Excel workbook") : qsTr("Import text or CSV")
    subtitle: String(source).replace(/^.*\//, "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: qsTr("Import")
    secondaryText: qsTr("Cancel")
    primaryEnabled: proposal.ok === true && (plan.table || "").length > 0

    function openFor(url, kind) {
        source = url
        format = kind || "csv"
        delimiter = ""
        sheet = ""
        headerRow = true
        intoExisting = false
        propose("")
        open()
    }
    function propose(table) {
        proposal = excel ? base.xlsxProposal(source, sheet, table, headerRow)
                         : base.csvProposal(source, table, delimiter, headerRow)
        if (!proposal.ok) { plan = ({ columns: [] }); fillPreview(); return }
        plan = proposal.plan
        intoExisting = !plan.createNew
        if (excel) sheet = proposal.sheet
        else if (delimiter === "") delimiter = proposal.delimiter === "tab" ? "tab" : proposal.delimiter
        fillPreview()
    }
    function setColumn(i, key, value) {
        const next = JSON.parse(JSON.stringify(plan))
        next.columns[i][key] = value
        plan = next
    }
    // The first rows as a Tk.DataTable (columns made per file, as SqlView does).
    function fillPreview() {
        const header = proposal.header || []
        const cols = []
        for (let i = 0; i < header.length; ++i)
            cols.push(dialog.columnComponent.createObject(previewTable, { key: "c" + i, title: header[i], width: Tk.Theme.size.tableColumn }))
        previewTable.columns = cols
        previewTable.model = (proposal.preview || []).map(function(row) {
            const record = {}
            for (let i = 0; i < header.length; ++i) record["c" + i] = row[i]
            return record
        })
    }
    onAccepted: {
        const r = excel ? base.importXlsx(source, plan, sheet, headerRow) : base.importCsv(source, plan, delimiter, headerRow)
        if (!r.ok) { window.notify(qsTr("Nothing was imported"), r.error, "danger"); return }
        window.baseHost.selectedKind = "table"
        window.baseHost.selectedName = r.table
        window.commands.openObject("table", r.table)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { visible: dialog.proposal.ok === false; variant: "danger"; text: dialog.proposal.error || "" }
        Tk.PropertyRow {
            label: qsTr("Into")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.Segmented {
                    small: true
                    model: [qsTr("A new table"), qsTr("An existing table")]
                    currentIndex: dialog.intoExisting ? 1 : 0
                    enabled: dialog.base !== null && dialog.base.tableNames.length > 0
                    onActivated: function(i) {
                        dialog.intoExisting = i === 1
                        dialog.propose(i === 1 ? dialog.base.tableNames[0] : "")
                    }
                }
                Tk.TextField {
                    objectName: "importTableName"
                    visible: !dialog.intoExisting
                    small: true
                    text: dialog.plan.table || ""
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onEditingFinished: if (text !== dialog.plan.table) dialog.propose(text)
                }
                Tk.ComboBox {
                    visible: dialog.intoExisting
                    small: true
                    model: dialog.base !== null ? dialog.base.tableNames : []
                    currentIndex: model.indexOf(dialog.plan.table || "")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.propose(model[i]) }
                }
            }
        }
        Tk.PropertyRow {
            label: dialog.excel ? qsTr("Sheet") : qsTr("File")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.ComboBox {
                    objectName: "importDelimiter"
                    visible: !dialog.excel
                    small: true
                    textRole: "label"
                    model: dialog.delimiters
                    currentIndex: Math.max(0, dialog.delimiters.findIndex(function(d) { return d.id === dialog.delimiter }))
                    tooltip: qsTr("Delimiter")
                    onActivated: function(i) { dialog.delimiter = dialog.delimiters[i].id; dialog.propose(dialog.plan.table || "") }
                }
                Tk.ComboBox {
                    objectName: "importSheet"
                    visible: dialog.excel
                    small: true
                    model: dialog.proposal.sheets || []
                    currentIndex: model.indexOf(dialog.sheet)
                    tooltip: qsTr("The sheet to import")
                    onActivated: function(i) { dialog.sheet = model[i]; dialog.propose("") }
                }
                Tk.CheckBox {
                    objectName: "importHeaderRow"
                    text: qsTr("First row holds the names")
                    checked: dialog.headerRow
                    onToggled: { dialog.headerRow = checked; dialog.propose(dialog.plan.table || "") }
                }
                Tk.Caption { text: qsTr("%n row(s)", "", dialog.proposal.rowCount || 0); Tk.Flex.shrink: 0 }
            }
        }
        Tk.Overline { title: qsTr("COLUMNS") }
        Repeater {
            model: dialog.plan.columns || []
            delegate: Tk.Flex {
                id: columnRow
                required property var modelData
                required property int index
                objectName: "importColumn_" + index
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.CheckBox {
                    checked: columnRow.modelData.include
                    tooltip: qsTr("Import this column")
                    onToggled: dialog.setColumn(columnRow.index, "include", checked)
                }
                Tk.TextField {
                    small: true
                    enabled: !dialog.intoExisting
                    text: columnRow.modelData.name
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onEditingFinished: if (text !== columnRow.modelData.name) dialog.setColumn(columnRow.index, "name", text)
                }
                Tk.ComboBox {
                    small: true
                    enabled: !dialog.intoExisting && !columnRow.modelData.primaryKey
                    textRole: "label"
                    model: columnRow.modelData.primaryKey ? [{ id: "autoNumber", label: qsTr("Key (kept)") }] : dialog.types
                    currentIndex: Math.max(0, model.findIndex(function(t) { return t.id === columnRow.modelData.type }))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.setColumn(columnRow.index, "type", model[i].id) }
                }
                Tk.Caption {
                    text: columnRow.modelData.samples || ""
                    elide: Text.ElideRight
                    Tk.Flex.grow: 1.4
                    Tk.Flex.basis: 0
                }
            }
        }
        Tk.Overline { title: qsTr("FIRST ROWS"); visible: (dialog.proposal.preview || []).length > 0 }
        Tk.DataTable {
            id: previewTable
            objectName: "importPreview"
            visible: (dialog.proposal.preview || []).length > 0
            Tk.Flex.basis: Tk.Theme.size.row * (Math.min(8, (dialog.proposal.preview || []).length) + 1.5)
            Tk.Flex.shrink: 0
            tooltip: qsTr("The first rows as they will be read")
        }
        Tk.CheckBox {
            visible: !dialog.intoExisting
            text: qsTr("Add an ID field that numbers the records")
            checked: dialog.plan.addId === true
            onToggled: { const next = Object.assign({}, dialog.plan); next.addId = checked; dialog.plan = next }
        }
        Tk.CheckBox {
            objectName: "importSkipBad"
            text: qsTr("Skip rows that don't fit (otherwise nothing is imported and the first bad row is named)")
            checked: dialog.plan.skipBadRows === true
            onToggled: { const next = Object.assign({}, dialog.plan); next.skipBadRows = checked; dialog.plan = next }
        }
    }
}
