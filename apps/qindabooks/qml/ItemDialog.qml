// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// A product or service you sell (or buy): its default description and
// price, which income account it credits, and whether sales tax applies.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var record: ({})
    property var incomeAccounts: []
    property var expenseAccounts: []
    readonly property var types: [{ text: qsTr("Service"), value: "service" }, { text: qsTr("Product"), value: "product" },
                                  { text: qsTr("Other"), value: "other" }]

    objectName: "itemDialog"
    title: Number(record.id || 0) > 0 ? String(record.name || "") : qsTr("New product or service")
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    primaryEnabled: nameField.text.trim().length > 0

    function openFor(id) {
        record = Number(id || 0) > 0 ? (books.items(true).find(function(i) { return i.id === id }) || ({})) : ({ type: "service", taxable: true })
        incomeAccounts = books.incomeAccounts()
        expenseAccounts = [{ id: 0, name: qsTr("None") }].concat(books.expenseAccounts())
        nameField.text = String(record.name || ""); descriptionField.text = String(record.description || "")
        priceField.text = String(record.price || ""); taxable.checked = record.taxable !== false
        typeBox.currentIndex = Math.max(0, types.map(function(t) { return t.value }).indexOf(String(record.type || "service")))
        error.visible = false
        open()
    }
    onAccepted: {
        const r = books.saveItem(Object.assign({}, record, { name: nameField.text.trim(), description: descriptionField.text,
            price: priceField.text, taxable: taxable.checked, type: types[typeBox.currentIndex].value,
            incomeAccountId: dialog.incomeAccounts.length ? dialog.incomeAccounts[incomeBox.currentIndex].id : 0,
            expenseAccountId: dialog.expenseAccounts[expenseBox.currentIndex].id }))
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Type"); Tk.Segmented { id: typeBox; small: true; model: dialog.types } }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "itemName" } }
        Tk.PropertyRow { label: qsTr("Description"); alignTop: true; Tk.TextArea { id: descriptionField; rows: 2; placeholderText: qsTr("Shown on invoices") } }
        Tk.PropertyRow { label: qsTr("Price"); Tk.TextField { id: priceField; objectName: "itemPrice"; mono: true; placeholderText: "0.00" } }
        Tk.PropertyRow { label: qsTr("Income account"); Tk.ComboBox { id: incomeBox; small: true; textRole: "name"; model: dialog.incomeAccounts
            currentIndex: Math.max(0, dialog.incomeAccounts.map(function(a) { return a.id }).indexOf(dialog.record.incomeAccountId || 0)) } }
        Tk.PropertyRow { label: qsTr("Cost account"); Tk.ComboBox { id: expenseBox; small: true; textRole: "name"; model: dialog.expenseAccounts
            currentIndex: Math.max(0, dialog.expenseAccounts.map(function(a) { return a.id }).indexOf(dialog.record.expenseAccountId || 0)) } }
        Tk.Switch { id: taxable; text: qsTr("Charge sales tax on this") }
    }
}
