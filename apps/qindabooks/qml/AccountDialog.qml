// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// One account of the chart of accounts: number, name, type, parent
// (sub-accounts), description, and an opening balance for a new balance
// sheet account. An account with postings can be made inactive, never
// deleted (D-045).
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var record: ({})
    property var types: []
    property var parents: []

    objectName: "accountDialog"
    title: Number(record.id || 0) > 0 ? String(record.fullName || record.name || "") : qsTr("New account")
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: Number(record.id || 0) > 0 && !record.systemRole ? (record.active === false ? qsTr("Make active") : qsTr("Make inactive")) : ""
    primaryEnabled: nameField.text.trim().length > 0

    function openFor(id) {
        types = books.accountTypes()
        record = Number(id || 0) > 0 ? (books.accounts(true).find(function(a) { return a.id === id }) || ({})) : ({ type: types.length ? types[0].id : "" })
        parents = [{ id: 0, fullName: qsTr("None (top level)") }].concat(books.accounts(false).filter(function(a) { return a.id !== record.id }))
        numberField.text = String(record.number || ""); nameField.text = String(record.name || "")
        descriptionField.text = String(record.description || ""); openingField.text = ""; openingDate.text = books.today
        error.visible = false
        open()
    }
    onAccepted: {
        const r = books.saveAccount(Object.assign({}, record, { number: numberField.text.trim(), name: nameField.text.trim(),
            type: dialog.types[typeBox.currentIndex].id, parentId: dialog.parents[parentBox.currentIndex].id,
            description: descriptionField.text, openingBalance: openingField.text, openingDate: openingDate.text }))
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
    }
    onTertiary: books.setAccountActive(record.id, record.active === false)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Type"); Tk.ComboBox { id: typeBox; objectName: "accountType"; small: true; textRole: "label"; model: dialog.types
            enabled: !dialog.record.systemRole
            currentIndex: Math.max(0, dialog.types.map(function(t) { return t.id }).indexOf(dialog.record.type)) } }
        Tk.PropertyRow { label: qsTr("Number"); Tk.TextField { id: numberField; objectName: "accountNumber"; mono: true; placeholderText: qsTr("Optional, e.g. 6300") } }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "accountName" } }
        Tk.PropertyRow { label: qsTr("Sub-account of"); Tk.ComboBox { id: parentBox; small: true; textRole: "fullName"; model: dialog.parents
            currentIndex: Math.max(0, dialog.parents.map(function(p) { return p.id }).indexOf(dialog.record.parentId || 0)) } }
        Tk.PropertyRow { label: qsTr("Description"); Tk.TextField { id: descriptionField } }
        Tk.PropertyRow {
            visible: Number(dialog.record.id || 0) === 0
            label: qsTr("Opening balance")
            hint: qsTr("Posted against Opening Balance Equity")
            Tk.Flex { direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                Tk.TextField { id: openingField; mono: true; placeholderText: "0.00"; Tk.Flex.grow: 1 }
                Tk.TextField { id: openingDate; mono: true; implicitWidth: Tk.Theme.size.fieldWidth } }
        }
    }
}
