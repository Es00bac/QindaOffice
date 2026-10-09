// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// One time activity (M68, D-077), QuickBooks' "Time/Enter Single
// Activity": date, who (an employee or a vendor), customer:job, service,
// duration (1:30, 1.5 or 90m), billable, notes. Billed time is read-only
// until it comes off its invoice.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var entry: ({})
    property var workers: []
    property var customers: []
    property var services: []
    readonly property bool billed: entry.status === "billed"
    readonly property bool existing: Number(entry.id || 0) > 0

    objectName: "timeActivityDialog"
    title: existing ? qsTr("Time activity") : qsTr("New time activity")
    subtitle: existing ? String(entry.statusLabel || "") : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: billed ? qsTr("Close") : qsTr("Save")
    secondaryText: billed ? "" : qsTr("Cancel")
    tertiaryText: existing && !billed ? qsTr("Delete") : ""
    primaryEnabled: billed || (workerBox.currentIndex > 0 && durationField.text.trim().length > 0)

    function openFor(id) {
        workers = [{ key: "", name: qsTr("Choose who worked") }].concat(books.workers().map(function(w) {
            return { key: w.key, name: w.name + " · " + w.kindLabel } }))
        customers = [{ id: 0, name: qsTr("(no customer:job)") }].concat(books.parties("customer", false))
        services = [{ id: 0, name: qsTr("(no service)") }].concat(books.items(false))
        entry = Number(id || 0) > 0 ? books.timeEntry(id) : ({ date: books.today, billable: true, duration: "" })
        // An inactive employee's old time still names them.
        if (entry.worker && workers.map(function(w) { return w.key }).indexOf(entry.worker) < 0)
            workers = workers.concat([{ key: entry.worker, name: entry.workerName + " · " + qsTr("inactive") }])
        durationField.text = String(entry.duration || "")
        notesField.text = String(entry.notes || "")
        error.visible = false
        open()
    }
    function change(field, value) { const next = Object.assign({}, entry); next[field] = value; entry = next }
    function save() {
        const r = books.saveTimeEntry(Object.assign({}, entry, { duration: durationField.text, notes: notesField.text }))
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
        return r.ok
    }
    onAccepted: if (!billed) save()
    onTertiary: {
        const r = books.deleteTimeEntry(entry.id)
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Notice { visible: dialog.billed; variant: "info"; title: qsTr("Billed")
                    text: qsTr("This time is on invoice %1. Remove it from the invoice (or void the invoice) to change it.").arg(dialog.entry.invoiceNumber || "") }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Date")
                Tk.TextField { objectName: "timeDate"; small: true; mono: true; readOnly: dialog.billed; text: String(dialog.entry.date || ""); onEditingFinished: dialog.change("date", text) }
            }
            Tk.PropertyRow {
                label: qsTr("Name")
                Tk.ComboBox { id: workerBox; objectName: "timeWorker"; small: true; enabled: !dialog.billed; textRole: "name"; model: dialog.workers
                              currentIndex: Math.max(0, dialog.workers.map(function(w) { return w.key }).indexOf(dialog.entry.worker || ""))
                              onActivated: function(i) { dialog.change("worker", dialog.workers[i].key) } }
            }
            Tk.PropertyRow {
                label: qsTr("Customer:job")
                Tk.ComboBox { objectName: "timeCustomer"; small: true; enabled: !dialog.billed; textRole: "name"; model: dialog.customers
                              currentIndex: Math.max(0, dialog.customers.map(function(c) { return c.id }).indexOf(dialog.entry.customerId || 0))
                              onActivated: function(i) { dialog.change("customerId", dialog.customers[i].id) } }
            }
            Tk.PropertyRow {
                label: qsTr("Service")
                Tk.ComboBox { objectName: "timeService"; small: true; enabled: !dialog.billed; textRole: "name"; model: dialog.services
                              currentIndex: Math.max(0, dialog.services.map(function(s) { return s.id }).indexOf(dialog.entry.itemId || 0))
                              onActivated: function(i) { dialog.change("itemId", dialog.services[i].id) } }
            }
            Tk.PropertyRow {
                label: qsTr("Duration")
                Tk.TextField { id: durationField; objectName: "timeDuration"; small: true; mono: true; readOnly: dialog.billed; placeholderText: qsTr("h:mm — 1:30, 1.5 or 90m") }
            }
            Tk.PropertyRow {
                label: qsTr("Billable")
                Tk.Switch { objectName: "timeBillable"; enabled: !dialog.billed; checked: dialog.entry.billable === true
                            tooltip: qsTr("Offer this time on the customer's next invoice")
                            onToggled: { dialog.change("billable", checked); checked = Qt.binding(function() { return dialog.entry.billable === true }) } }
            }
        }
        Tk.TextArea { id: notesField; objectName: "timeNotes"; rows: 3; readOnly: dialog.billed; placeholderText: qsTr("Notes (they become the invoice line's description)") }
    }
}
