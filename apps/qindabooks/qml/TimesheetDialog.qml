// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// The weekly timesheet (M68, D-077), QuickBooks' "Use Weekly Timesheet":
// one person's week, Monday first — a row per customer:job, service,
// billable and notes, a duration per day (1:30, 1.5 or 90m), row and day
// totals. Saving writes the whole week in one go (time_tracking.h); a
// billed cell is read-only. Durations stay text; the service adds them up.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var workers: []
    property string worker: ""
    property var sheet: ({})
    property var rows: []          // editable copy of sheet.rows
    property var customers: []
    property var services: []

    objectName: "timesheetDialog"
    title: qsTr("Weekly timesheet")
    subtitle: sheet.weekStart ? qsTr("Week of %1").arg(sheet.weekStart) : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 2.6
    primaryText: qsTr("Save")
    secondaryText: qsTr("Close")
    primaryEnabled: worker.length > 0

    function openDialog(workerKey, dateIso) {
        workers = books.workers().map(function(w) { return { key: w.key, name: w.name + " · " + w.kindLabel } })
        customers = [{ id: 0, name: qsTr("(no customer:job)") }].concat(books.parties("customer", false))
        services = [{ id: 0, name: qsTr("(no service)") }].concat(books.items(false))
        worker = workerKey || (workers.length > 0 ? workers[0].key : "")
        load(dateIso || books.today)
        open()
    }
    function load(dateIso) {
        sheet = books.timesheet(worker, dateIso)
        rows = (sheet.rows || []).map(function(r) { return Object.assign({}, r, { cells: r.cells.slice() }) })
        if (rows.length === 0) addRow()
        error.visible = false
    }
    function shift(days) {
        // UTC arithmetic on the ISO date: no time zone can move it a day.
        const d = new Date(sheet.weekStart + "T00:00:00Z")
        d.setUTCDate(d.getUTCDate() + days)
        load(d.toISOString().slice(0, 10))
    }
    function addRow() {
        const cells = []
        for (let i = 0; i < 7; ++i) cells.push({ entryId: 0, duration: "", billed: false })
        rows = rows.concat([{ customerId: 0, itemId: 0, billable: false, notes: "", cells: cells }])
    }
    function changeRow(index, field, value) {
        const next = rows.slice()
        next[index] = Object.assign({}, next[index], { [field]: value })
        rows = next
    }
    function changeCell(index, day, text) {
        const next = rows.slice()
        const cells = next[index].cells.slice()
        cells[day] = Object.assign({}, cells[day], { duration: text })
        next[index] = Object.assign({}, next[index], { cells: cells })
        rows = next
    }
    function removeRow(index) {
        // Emptying the cells deletes the row's entries when it saves.
        const next = rows.slice()
        next[index] = Object.assign({}, next[index], { cells: next[index].cells.map(function(c) { return Object.assign({}, c, { duration: "" }) }), removed: true })
        rows = next
    }
    function save() {
        const r = books.saveTimesheet({ worker: worker, weekStart: sheet.weekStart, rows: rows })
        if (r.ok) load(sheet.weekStart)
        error.variant = r.ok ? "success" : "danger"
        error.title = r.ok ? qsTr("Saved") : qsTr("Not saved")
        error.text = r.ok ? qsTr("The week holds %1.").arg(sheet.totalText) : String(r.error || "")
        error.visible = true
        return r.ok
    }
    onAccepted: { save(); open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "timesheetNotice"; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox { objectName: "timesheetWorker"; small: true; Tk.Flex.basis: Tk.Theme.size.tableColumn * 3; textRole: "name"; model: dialog.workers
                          currentIndex: Math.max(0, dialog.workers.map(function(w) { return w.key }).indexOf(dialog.worker))
                          onActivated: function(i) { dialog.worker = dialog.workers[i].key; dialog.load(dialog.sheet.weekStart) } }
            Tk.Spacer { }
            Tk.IconButton { objectName: "timesheetPrevious"; small: true; iconName: "chevron-left"; tooltip: qsTr("Previous week"); onClicked: dialog.shift(-7) }
            Tk.Mono { text: String(dialog.sheet.weekStart || "") }
            Tk.IconButton { objectName: "timesheetNext"; small: true; iconName: "chevron-right"; tooltip: qsTr("Next week"); onClicked: dialog.shift(7) }
        }
        // The grid keeps its columns and scrolls sideways when the window is narrow.
        Tk.Scroll {
            id: gridScroll
            objectName: "timesheetScroll"
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Hidden
            implicitHeight: grid.implicitHeight + Tk.Theme.space.md // room for the overlay scrollbar
            // AGENT-NOTE: a plain Item carries the minimum width — a Flex sets
            // its own implicit width — and Tk.Scroll stretches it to a wider viewport.
            Item {
                implicitWidth: Tk.Theme.size.dialogWidth * 2.3
                implicitHeight: grid.implicitHeight
                Tk.Flex {
                    id: grid
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.Overline { title: qsTr("Customer:job"); Tk.Flex.flex: 3 }
                        Tk.Overline { title: qsTr("Service"); Tk.Flex.flex: 3 }
                        Tk.Overline { title: qsTr("Notes"); Tk.Flex.flex: 3 }
                        Tk.Overline { title: qsTr("Bill"); Tk.Flex.basis: Tk.Theme.size.control * 1.5; Tk.Flex.shrink: 0 }
                        Repeater {
                            model: dialog.sheet.days || []
                            delegate: Tk.Overline { required property var modelData; title: String(modelData.label); Tk.Flex.flex: 1; horizontalAlignment: Text.AlignRight }
                        }
                        Tk.Overline { title: qsTr("Total"); Tk.Flex.flex: 1; horizontalAlignment: Text.AlignRight }
                        Item { implicitWidth: Tk.Theme.size.control }
                    }
                    Tk.Divider { }
                    Repeater {
                        model: dialog.rows.length
                        delegate: Tk.Flex {
                            id: row
                            required property int index
                            readonly property var rowData: dialog.rows[index] || ({})
                            objectName: "timesheetRow_" + index
                            visible: rowData.removed !== true
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.xs
                            Tk.ComboBox { Tk.Flex.flex: 3; small: true; textRole: "name"; model: dialog.customers
                                          currentIndex: Math.max(0, dialog.customers.map(function(c) { return c.id }).indexOf(row.rowData.customerId || 0))
                                          onActivated: function(i) { dialog.changeRow(row.index, "customerId", dialog.customers[i].id) } }
                            Tk.ComboBox { Tk.Flex.flex: 3; small: true; textRole: "name"; model: dialog.services
                                          currentIndex: Math.max(0, dialog.services.map(function(s) { return s.id }).indexOf(row.rowData.itemId || 0))
                                          onActivated: function(i) { dialog.changeRow(row.index, "itemId", dialog.services[i].id) } }
                            Tk.TextField { Tk.Flex.flex: 3; small: true; text: String(row.rowData.notes || ""); placeholderText: qsTr("Notes")
                                           onEditingFinished: if (text !== String(row.rowData.notes || "")) dialog.changeRow(row.index, "notes", text) }
                            Tk.CheckBox { implicitWidth: Tk.Theme.size.control * 1.5; checked: row.rowData.billable === true; tooltip: qsTr("Billable")
                                          onToggled: { dialog.changeRow(row.index, "billable", checked); checked = Qt.binding(function() { return row.rowData.billable === true }) } }
                            Repeater {
                                model: 7
                                delegate: Tk.TextField {
                                    id: cell
                                    required property int index
                                    readonly property var value: (row.rowData.cells || [])[index] || ({})
                                    objectName: "timesheetCell_" + row.index + "_" + index
                                    Tk.Flex.flex: 1
                                    small: true
                                    mono: true
                                    readOnly: cell.value.billed === true
                                    horizontalAlignment: Text.AlignRight
                                    tooltip: cell.value.billed === true ? qsTr("Billed on an invoice") : ""
                                    text: String(cell.value.duration || "")
                                    onEditingFinished: if (text !== String(cell.value.duration || "")) dialog.changeCell(row.index, index, text.trim())
                                }
                            }
                            Tk.Mono { Tk.Flex.flex: 1; horizontalAlignment: Text.AlignRight; text: String(row.rowData.totalText || "") }
                            Tk.IconButton { small: true; iconName: "x"; tooltip: qsTr("Remove this row"); enabled: !(row.rowData.cells || []).some(function(c) { return c.billed })
                                            onClicked: dialog.removeRow(row.index) }
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.Button { objectName: "timesheetAddRow"; small: true; variant: "ghost"; iconName: "plus"; text: qsTr("Add a row"); onClicked: dialog.addRow(); Tk.Flex.flex: 9 }
                        Item { implicitWidth: Tk.Theme.size.control * 1.5 }
                        Repeater {
                            model: dialog.sheet.dayTotals || []
                            delegate: Tk.Mono { required property var modelData; text: String(modelData); Tk.Flex.flex: 1; horizontalAlignment: Text.AlignRight }
                        }
                        Tk.Mono { objectName: "timesheetTotal"; Tk.Flex.flex: 1; horizontalAlignment: Text.AlignRight; text: String(dialog.sheet.totalText || "") }
                        Item { implicitWidth: Tk.Theme.size.control }
                    }
                }
            }
        }
    }
}
