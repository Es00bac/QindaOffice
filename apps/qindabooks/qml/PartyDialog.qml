// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// A customer or vendor record: who they are, how to reach them, payment
// terms, and — for vendors — the 1099 flag and their usual category.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string kind: "customer"
    property var record: ({})
    property var terms: []
    property var categories: []

    objectName: "partyDialog"
    title: Number(record.id || 0) > 0 ? String(record.name || "") : kind === "customer" ? qsTr("New customer") : qsTr("New vendor")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: Number(record.id || 0) > 0 ? (record.active === false ? qsTr("Make active") : qsTr("Make inactive")) : ""
    primaryEnabled: nameField.text.trim().length > 0

    function openFor(k, id) {
        kind = k
        record = Number(id || 0) > 0 ? books.party(id) : ({ kind: k, active: true })
        terms = [{ id: 0, name: qsTr("Company default") }].concat(books.terms())
        categories = [{ id: 0, name: qsTr("None") }].concat(books.expenseAccounts())
        nameField.text = String(record.name || ""); companyField.text = String(record.company || "")
        emailField.text = String(record.email || ""); phoneField.text = String(record.phone || "")
        addressField.text = String(record.address || ""); taxIdField.text = String(record.taxId || "")
        notesField.text = String(record.notes || ""); vendor1099.checked = record.is1099 === true
        error.visible = false
        open()
        nameField.forceActiveFocus()
    }
    function collect(extra) {
        return Object.assign({}, record, { kind: kind, name: nameField.text.trim(), company: companyField.text.trim(),
            email: emailField.text.trim(), phone: phoneField.text.trim(), address: addressField.text, taxId: taxIdField.text.trim(),
            notes: notesField.text, termsId: dialog.terms[termsBox.currentIndex].id, is1099: vendor1099.checked,
            defaultAccountId: dialog.kind === "vendor" ? dialog.categories[categoryBox.currentIndex].id : 0 }, extra || {})
    }
    onAccepted: { const r = books.saveParty(collect()); if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() } }
    onTertiary: books.saveParty(collect({ active: record.active === false }))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "partyName"; placeholderText: qsTr("Display name") } }
        Tk.PropertyRow { label: qsTr("Company"); Tk.TextField { id: companyField; objectName: "partyCompany" } }
        Tk.PropertyRow { label: qsTr("Email"); Tk.TextField { id: emailField; objectName: "partyEmail"; iconName: "mail"; placeholderText: qsTr("Invoices are emailed here") } }
        Tk.PropertyRow { label: qsTr("Phone"); Tk.TextField { id: phoneField; objectName: "partyPhone" } }
        Tk.PropertyRow { label: qsTr("Address"); alignTop: true; Tk.TextArea { id: addressField; objectName: "partyAddress"; rows: 3 } }
        Tk.PropertyRow { label: qsTr("Terms"); Tk.ComboBox { id: termsBox; small: true; textRole: "name"; model: dialog.terms
            currentIndex: Math.max(0, dialog.terms.map(function(t) { return t.id }).indexOf(dialog.record.termsId || 0)) } }
        Tk.PropertyRow { label: qsTr("Tax ID"); Tk.TextField { id: taxIdField; objectName: "partyTaxId" } }
        Tk.PropertyRow { visible: dialog.kind === "vendor"; label: qsTr("Usual category"); Tk.ComboBox { id: categoryBox; small: true; textRole: "name"; model: dialog.categories
            currentIndex: Math.max(0, dialog.categories.map(function(c) { return c.id }).indexOf(dialog.record.defaultAccountId || 0)) } }
        Tk.Switch { id: vendor1099; visible: dialog.kind === "vendor"; text: qsTr("Track payments for a 1099") }
        Tk.TextArea { id: notesField; rows: 2; placeholderText: qsTr("Notes") }
    }
}
