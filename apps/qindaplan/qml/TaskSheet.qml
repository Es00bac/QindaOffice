// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The task sheet: Tk.EditableGrid over PlanService's task grid model (rows
// are task GUIDs plus the blank "new" row; columns the current table's
// fields). Edits go to the engine through the grid's domain source as one
// undo step each; the selection follows the grid's ranges. M89 (D-097):
// with View ▸ Group By, a band's header row (QindaTK D-312) comes before its
// tasks, with the band's rollups in their columns; its chevron (or Return,
// Space) shows or hides them — the same rows as the Gantt beside it.
Tk.EditableGrid {
    id: sheet
    objectName: "planTaskSheet"

    property PlanService plan: null
    // Column titles by field id, from the service.
    readonly property var titles: {
        const out = {}
        for (const c of (plan !== null ? plan.sheetColumns : [])) out[c.id] = c.title
        return out
    }

    model: plan !== null ? plan.sheetModel : null
    tooltip: qsTr("Task sheet")
    rowHeaderWidth: Tk.Theme.size.control * 1.6
    fillHandleEnabled: false
    columnLabel: function(id, index) { return sheet.titles[id] !== undefined ? sheet.titles[id] : id }
    rowLabel: function(id, index) {
        return id === "new" || sheet.plan === null ? "" : sheet.plan.taskFieldText(id, "id")
    }
    groupHeader: function(rowId, index) {
        if (sheet.plan === null) return null
        void sheet.plan.revision // re-read the bands when the plan changes
        return sheet.plan.groupHeader(rowId)
    }
    onGroupToggled: function(rowId, collapsed) { if (sheet.plan !== null) sheet.plan.setGroupCollapsed(rowId, collapsed) }
    // View ▸ Highlight's fill: a theme tint, never a literal (rule 3).
    Binding {
        target: sheet.plan
        when: sheet.plan !== null
        property: "highlightFill"
        value: Tk.Theme.mix(Tk.Theme.color.panel, Tk.Theme.color.warning, Tk.Theme.opacity.tintStrong)
    }

    // Scrolls the row into view and makes it current.
    function reveal(taskId) {
        if (model === null) return
        const row = model.rowIndex(taskId)
        if (row < 0) return
        const column = Math.max(0, model.columnIndex("name"))
        setCurrent(row, column, false)
        ensureCurrentVisible()
    }
    function applyWidths() {
        if (plan === null || model === null) return
        for (const c of plan.sheetColumns) model.requestColumnResize(c.id, c.factor * Tk.Theme.size.tableColumn)
    }
    // The selected task rows (the active one first), from the grid ranges.
    function selectedIds() {
        const ids = []
        if (activeRowId !== "" && activeRowId !== "new") ids.push(activeRowId)
        for (const range of model.ranges) {
            for (let r = range.firstRow; r <= range.lastRow; ++r) {
                const id = model.rowIdAt(r)
                if (id !== "new" && ids.indexOf(id) < 0) ids.push(id)
            }
        }
        return ids
    }

    onCurrentCellChanged: function(rowId, columnId) {
        if (plan === null) return
        plan.selectedTaskIds = selectedIds()
    }
    Connections {
        target: sheet.plan
        function onSelectionChanged() {
            const id = sheet.plan.currentTaskId
            if (id !== "" && id !== sheet.activeRowId && sheet.model !== null && sheet.model.rowIndex(id) >= 0) {
                sheet.setCurrent(sheet.model.rowIndex(id), Math.max(0, sheet.activeColumn >= 0 ? sheet.activeColumn
                                                                     : sheet.model.columnIndex("name")), false)
            }
        }
        function onViewChanged() { sheet.applyWidths() }
    }
    Component.onCompleted: applyWidths()
    onModelChanged: applyWidths()
}
