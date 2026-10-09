// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// A table's or a query's rows in a Tk.EditableGrid over the service's
// DatasheetModel (datasheet_model.h): typing edits (each commit one undo
// step through base_engine), the last row "✱" adds a record, the header
// carries sort/filter state, the footer the totals row and record
// navigation. A PivotTable view of the same rows reuses QindaTK's
// QueryBuilder and PivotView (PivotPanel.qml).
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    readonly property DatasheetModel sheet: base !== null ? base.datasheet : null
    property bool filterBarVisible: false
    property bool findVisible: false
    property string mode: "datasheet" // datasheet | pivot
    readonly property alias grid: grid
    // The current cell when it is a file field of a saved record (M65).
    readonly property var fileColumn: {
        if (sheet === null || sheet.sourceKind !== "table" || grid.activeRowId.length === 0 || grid.activeRowId === "new") return null
        const columns = sheet.columns
        for (let i = 0; i < columns.length; ++i)
            if (columns[i].name === grid.activeColumnId && columns[i].type === "file") return columns[i]
        return null
    }
    readonly property var fileTarget: fileColumn === null ? ({})
        : { table: sheet.sourceName, rowid: Number(grid.activeRowId.slice(1)), field: fileColumn.name, hasFile: filePanel.hasFile }
    onFileTargetChanged: if (window !== null && visible) window.baseHost.fileTarget = fileTarget
    onVisibleChanged: if (window !== null && visible) window.baseHost.fileTarget = fileTarget

    function refresh() { if (sheet !== null) sheet.refresh() }
    function activeRange() {
        const ranges = sheet.grid.ranges
        if (ranges.length > 0) return ranges[0]
        return { firstRow: grid.activeRow, lastRow: grid.activeRow, firstColumn: grid.activeColumn, lastColumn: grid.activeColumn }
    }
    function selectedRowIds() {
        const r = activeRange()
        const out = []
        for (let i = Math.max(0, r.firstRow); i <= r.lastRow && i < sheet.recordCount; ++i) out.push(sheet.grid.rowIdAt(i))
        return out
    }
    function newRecord() {
        if (!sheet.editable) return
        let column = 0
        const columns = sheet.columns
        while (column < columns.length && columns[column].readOnly) ++column
        grid.setCurrent(sheet.grid.rowCount - 1, Math.min(column, columns.length - 1), false)
        grid.ensureCurrentVisible()
        grid.forceActiveFocus()
    }
    function saveRecord() {
        if (grid.cellEditing) grid.commitCellEdit(grid.editText)
        if (sheet.pendingMessage.length > 0 || grid.activeRowId === "new") {
            const r = sheet.savePendingRecord()
            if (!r.ok && r.error) view.window.notify(qsTr("Record not saved"), r.error, "warning")
        }
    }
    function deleteRecord() {
        const ids = selectedRowIds()
        if (ids.length === 0) return
        const impact = sheet.deleteImpact(ids)
        if (impact.blocked) { view.window.notify(qsTr("Cannot delete"), impact.lines.join(" "), "warning"); return }
        view.window.confirmDialog.ask(qsTr("Delete %n record(s)?", "", ids.length),
                                      impact.lines.concat([qsTr("Undo brings them back.")]).join("\n"),
                                      "danger", qsTr("Delete"), function() {
            const r = sheet.deleteRows(ids)
            if (!r.ok) view.window.notify(qsTr("Not deleted"), r.error, "danger")
        })
    }
    function sortActive(descending) {
        if (grid.activeColumnId) sheet.sortBy(grid.activeColumnId, descending)
    }
    function filterBySelection(exclude) {
        if (grid.activeRowId && grid.activeRowId !== "new" && grid.activeColumnId
                && !sheet.filterBySelection(grid.activeRowId, grid.activeColumnId, exclude))
            view.window.notify(qsTr("Filter"), sheet.error, "warning")
    }
    function copySelection(cut) {
        const r = activeRange()
        view.window.baseHost.copyToClipboard(sheet.copyText(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn))
        if (cut && sheet.editable) {
            const done = sheet.clearCells(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
            if (!done.ok) view.window.notify(qsTr("Not cut"), done.error, "warning")
        }
    }
    function paste() {
        if (!grid.activeRowId || !grid.activeColumnId) return
        const r = sheet.pasteText(grid.activeRowId, grid.activeColumnId, view.window.baseHost.clipboardText())
        if (!r.ok) view.window.notify(qsTr("Not pasted"), r.error, "warning")
        else view.window.notify(qsTr("Pasted"), qsTr("%1 updated, %2 added").arg(r.updated).arg(r.inserted), "success")
    }
    function selectAll() {
        if (sheet.grid.rowCount === 0 || sheet.grid.columnCount === 0) return
        grid.setCurrent(0, 0, false)
        grid.setCurrent(Math.max(0, sheet.recordCount - 1), sheet.grid.columnCount - 1, true)
    }
    function startFind() {
        findVisible = true
        findField.forceActiveFocus()
        findField.selectAll()
    }
    function findNext(forward) {
        if (findField.text.length === 0) { startFind(); return }
        const hit = sheet.find(findField.text, grid.activeRowId, grid.activeColumnId, forward)
        if (!hit.found) { view.window.notify(qsTr("Find"), qsTr("“%1” was not found.").arg(findField.text), "info"); return }
        grid.setCurrent(hit.row, hit.column, false)
        grid.ensureCurrentVisible()
    }
    // M80 (a macro's GoToRecord and FindRecord).
    function goToRecord(which) {
        if (sheet === null) return
        if (which === "new") { newRecord(); return }
        const last = Math.max(0, sheet.recordCount - 1)
        const row = which === "first" ? 0 : which === "last" ? last
                  : which === "next" ? Math.min(last, grid.activeRow + 1) : Math.max(0, grid.activeRow - 1)
        grid.setCurrent(row, Math.max(0, grid.activeColumn), false)
        grid.ensureCurrentVisible()
    }
    function findText(text) {
        findVisible = true
        findField.text = text
        findNext(true)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "datasheetHeader"
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            paddingLeft: Tk.Theme.space.md
            paddingRight: Tk.Theme.space.md
            paddingTop: Tk.Theme.space.sm
            paddingBottom: Tk.Theme.space.sm
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                Tk.Icon {
                    name: view.sheet !== null && view.sheet.sourceKind === "query" ? "funnel" : "table"
                    size: Tk.Theme.size.icon
                    color: Tk.Theme.color.accent
                }
                Tk.Heading {
                    objectName: "datasheetTitle"
                    level: 3
                    text: view.sheet !== null ? view.sheet.sourceName : ""
                    Tk.Flex.shrink: 0
                }
                Tk.Badge {
                    visible: view.sheet !== null && !view.sheet.editable && view.sheet.sourceKind.length > 0
                    text: qsTr("read-only")
                }
                Repeater {
                    model: view.sheet !== null ? view.sheet.filters : []
                    delegate: Tk.Chip {
                        required property var modelData
                        small: true
                        iconName: "funnel"
                        emphasis: "tinted"
                        text: view.sheet.columnLabel(modelData.field) + " "
                              + ({ eq: "=", ne: "≠", lt: "<", le: "≤", gt: ">", ge: "≥", contains: "∋", notContains: "∌",
                                   isNull: qsTr("is empty"), isNotNull: qsTr("is not empty") })[modelData.op]
                              + (modelData.value ? " " + modelData.value : "")
                        tooltip: qsTr("Clear the filters")
                        onClicked: view.sheet.clearFilters()
                    }
                }
                Tk.Spacer { }
                Tk.SearchField {
                    id: findField
                    objectName: "datasheetFind"
                    visible: view.findVisible
                    small: true
                    placeholderText: qsTr("Find in datasheet")
                    tooltip: qsTr("Find text in the rows shown")
                    onAccepted: view.findNext(true)
                    Keys.onEscapePressed: { view.findVisible = false; grid.forceActiveFocus() }
                }
                Tk.Segmented {
                    objectName: "datasheetMode"
                    small: true
                    model: [{ text: qsTr("Datasheet"), iconName: "table", value: "datasheet" },
                            { text: qsTr("PivotTable"), iconName: "layout-grid", value: "pivot" }]
                    currentIndex: view.mode === "pivot" ? 1 : 0
                    tooltip: qsTr("Show the rows, or summarise them in a PivotTable")
                    onActivated: function(index) {
                        view.mode = index === 1 ? "pivot" : "datasheet"
                        if (view.mode === "pivot") view.sheet.preparePivot()
                    }
                }
            }
        }
        Tk.FilterBar {
            id: filterBar
            objectName: "datasheetFilterBar"
            visible: view.filterBarVisible && view.mode === "datasheet"
            Tk.Flex.shrink: 0
            width: view.width
            fields: view.sheet !== null ? view.sheet.columns.map(function(c) { return { id: c.name, label: c.label, type: c.filterType } }) : []
            onClausesEdited: function(next) { clauses = next }
            onConjunctionEdited: function(next) { conjunction = next }
            onPredicateChanged: if (valid) view.sheet.setFilters(clauses, conjunction)
        }
        Tk.Notice {
            objectName: "datasheetNotice"
            Tk.Flex.shrink: 0
            visible: view.sheet !== null && (view.sheet.error.length > 0 || view.sheet.pendingMessage.length > 0)
            variant: view.sheet !== null && view.sheet.error.length > 0 ? "danger" : "info"
            text: view.sheet === null ? "" : view.sheet.error.length > 0 ? view.sheet.error : view.sheet.pendingMessage
        }
        Tk.Flex {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: 0
            Item {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.EditableGrid {
                    id: grid
                    objectName: "datasheetGrid"
                    anchors.fill: parent
                    visible: view.mode === "datasheet"
                    model: view.sheet !== null ? view.sheet.grid : null
                    tooltip: view.sheet !== null ? view.sheet.sourceName : qsTr("Datasheet")
                    alternatingRows: true
                    fillHandleEnabled: false
                    rowHeaderWidth: Tk.Theme.size.control * 1.6
                    columnLabel: function(id, index) { return view.sheet !== null ? view.sheet.columnLabel(id) : id }
                    rowLabel: function(id, index) { return id === "new" ? "✱" : String(index + 1) }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Escape && grid.activeRowId === "new" && !grid.cellEditing) {
                            view.sheet.cancelPendingRecord()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Delete && !grid.cellEditing && view.sheet.editable) {
                            const r = view.activeRange()
                            view.sheet.clearCells(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
                            event.accepted = true
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: view.mode === "datasheet"
                    onTapped: function(point) { contextMenu.popup(grid, point.position.x, point.position.y) }
                }
                PivotPanel {
                    anchors.fill: parent
                    visible: view.mode === "pivot"
                    sheet: view.sheet
                }
                Tk.EmptyState {
                    anchors.centerIn: parent
                    visible: view.sheet !== null && view.sheet.recordCount === 0 && !view.sheet.editable
                             && view.sheet.sourceKind.length > 0 && view.mode === "datasheet"
                    iconName: "search"
                    title: qsTr("No records")
                    text: view.sheet !== null && view.sheet.filters.length > 0 ? qsTr("Nothing matches the filters.")
                                                                               : qsTr("The query returned no rows.")
                }
            }
            FileFieldPanel {
                id: filePanel
                objectName: "datasheetFilePanel"
                window: view.window
                visible: view.fileColumn !== null && view.mode === "datasheet"
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth
                Tk.Flex.shrink: 0
                table: view.fileColumn !== null ? view.sheet.sourceName : ""
                rowid: view.fileColumn !== null ? Number(view.grid.activeRowId.slice(1)) : 0
                field: view.fileColumn !== null ? view.fileColumn.name : ""
                label: view.fileColumn !== null ? view.fileColumn.label : ""
                editable: view.sheet !== null && view.sheet.editable
            }
        }
        DatasheetFooter {
            Tk.Flex.shrink: 0
            visible: view.mode === "datasheet"
            width: view.width
            sheet: view.sheet
            grid: grid
            window: view.window
        }
    }

    Tk.Menu {
        id: contextMenu
        Tk.MenuItem { text: qsTr("Sort A to Z"); iconName: "arrow-up-a-z"; onTriggered: view.sortActive(false) }
        Tk.MenuItem { text: qsTr("Sort Z to A"); iconName: "arrow-down-z-a"; onTriggered: view.sortActive(true) }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("Filter by Selection"); iconName: "funnel"; enabled: view.sheet !== null && view.sheet.sourceKind === "table"; onTriggered: view.filterBySelection(false) }
        Tk.MenuItem { text: qsTr("Filter Excluding Selection"); iconName: "circle-off"; enabled: view.sheet !== null && view.sheet.sourceKind === "table"; onTriggered: view.filterBySelection(true) }
        Tk.MenuItem { text: qsTr("Clear Sort and Filters"); iconName: "eraser"; onTriggered: view.sheet.clearFilters() }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("Copy"); iconName: "copy"; onTriggered: view.copySelection(false) }
        Tk.MenuItem { text: qsTr("Paste"); iconName: "clipboard-paste"; enabled: view.sheet !== null && view.sheet.editable; onTriggered: view.paste() }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("Delete Record"); iconName: "list-minus"; danger: true; enabled: view.sheet !== null && view.sheet.editable; onTriggered: view.deleteRecord() }
    }
    Connections {
        target: view.sheet
        function onSourceChanged() {
            if (view.mode === "pivot") view.sheet.preparePivot()
            // A freshly opened source starts on its first record.
            if (grid.activeRowId.length === 0 || grid.activeRow < 0) Qt.callLater(function() { grid.setCurrent(0, 0, false) })
        }
        function onRowsChanged() { if (view.mode === "pivot") view.sheet.preparePivot() }
    }
}
