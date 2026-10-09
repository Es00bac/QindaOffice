// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// An employee (M68, D-077): the minimal record time is entered for — name,
// how to reach them, an optional default hourly rate that their billable
// time bills at (else the service's price). No pay or tax data: payroll
// (M71) extends this list in its own schema version.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var record: ({})

    objectName: "employeeDialog"
    title: Number(record.id || 0) > 0 ? String(record.name || "") : qsTr("New employee")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: Number(record.id || 0) > 0 ? (record.active === false ? qsTr("Make active") : qsTr("Make inactive")) : ""
    primaryEnabled: nameField.text.trim().length > 0

    function openFor(id) {
        record = Number(id || 0) > 0 ? books.employee(id) : ({ active: true })
        nameField.text = String(record.name || ""); emailField.text = String(record.email || "")
        phoneField.text = String(record.phone || ""); addressField.text = String(record.address || "")
        rateField.text = String(record.rate || ""); notesField.text = String(record.notes || "")
        error.visible = false
        open()
        nameField.forceActiveFocus()
    }
    function collect(extra) {
        return Object.assign({ id: record.id || 0, name: nameField.text.trim(), email: emailField.text.trim(), phone: phoneField.text.trim(),
                               address: addressField.text, rate: rateField.text.trim(), notes: notesField.text }, extra || {})
    }
    function save(extra) {
        const r = books.saveEmployee(collect(extra))
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
        return r.ok
    }
    onAccepted: save()
    onTertiary: save({ active: record.active === false })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "employeeName"; placeholderText: qsTr("As it shows on time and reports") } }
        Tk.PropertyRow { label: qsTr("Email"); Tk.TextField { id: emailField; objectName: "employeeEmail"; iconName: "mail" } }
        Tk.PropertyRow { label: qsTr("Phone"); Tk.TextField { id: phoneField; objectName: "employeePhone" } }
        Tk.PropertyRow { label: qsTr("Address"); alignTop: true; Tk.TextArea { id: addressField; objectName: "employeeAddress"; rows: 3 } }
        Tk.PropertyRow {
            label: qsTr("Hourly rate")
            hint: qsTr("What their billable time bills at; leave empty to use the service's price.")
            Tk.TextField { id: rateField; objectName: "employeeRate"; mono: true; placeholderText: qsTr("Service price") }
        }
        Tk.TextArea { id: notesField; objectName: "employeeNotes"; rows: 2; placeholderText: qsTr("Notes") }
    }
}
