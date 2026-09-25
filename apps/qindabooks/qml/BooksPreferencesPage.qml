// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Settings ▸ Preferences ▸ QindaBooks: this company's details as they
// appear on invoices and statements, invoice numbering and default terms,
// the notes and payment instructions printed on every invoice, and the
// accent colour of the printed forms. Saved to the company file on Apply
// (D-014: every field is read by the forms or the sales engine).
Tk.Flex {
    id: page

    property var host: null
    readonly property var books: host !== null && host.books !== undefined ? host.books : null
    property var terms: []

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function load() {
        if (books === null || !books.hasCompany) return
        const s = books.settings
        terms = books.terms()
        nameField.text = String(s.name || ""); legalField.text = String(s.legalName || "")
        addressField.text = String(s.address || ""); emailField.text = String(s.email || "")
        phoneField.text = String(s.phone || ""); websiteField.text = String(s.website || "")
        taxIdField.text = String(s.taxId || ""); prefixField.text = String(s.invoicePrefix || "")
        nextField.text = String(s.nextInvoiceNumber || ""); notesField.text = String(s.invoiceNotes || "")
        paymentField.text = String(s.paymentInstructions || ""); accent.color = String(s.accentColor || "#1f5fbf")
        termsBox.currentIndex = Math.max(0, terms.map(function(t) { return t.id }).indexOf(s.defaultTermsId || 0))
    }
    function apply() {
        if (books === null || !books.hasCompany) return
        books.saveSettings(Object.assign({}, books.settings, {
            name: nameField.text.trim(), legalName: legalField.text.trim(), address: addressField.text,
            email: emailField.text.trim(), phone: phoneField.text.trim(), website: websiteField.text.trim(),
            taxId: taxIdField.text.trim(), invoicePrefix: prefixField.text.trim(), nextInvoiceNumber: Number(nextField.text) || 0,
            defaultTermsId: page.terms.length ? page.terms[termsBox.currentIndex].id : 0,
            invoiceNotes: notesField.text, paymentInstructions: paymentField.text, accentColor: String(accent.color) }))
    }
    onHostChanged: load()
    Component.onCompleted: load()

    Tk.Notice { visible: page.books === null || !page.books.hasCompany; variant: "info"; title: qsTr("No company open"); text: qsTr("Open or create a company to edit its settings.") }
    Tk.SectionHeader { title: qsTr("Company") }
    Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "prefCompanyName" } }
    Tk.PropertyRow { label: qsTr("Legal name"); Tk.TextField { id: legalField } }
    Tk.PropertyRow { label: qsTr("Address"); alignTop: true; Tk.TextArea { id: addressField; rows: 3 } }
    Tk.PropertyRow { label: qsTr("Email"); Tk.TextField { id: emailField } }
    Tk.PropertyRow { label: qsTr("Phone"); Tk.TextField { id: phoneField } }
    Tk.PropertyRow { label: qsTr("Website"); Tk.TextField { id: websiteField } }
    Tk.PropertyRow { label: qsTr("Tax ID"); Tk.TextField { id: taxIdField } }
    Tk.SectionHeader { title: qsTr("Invoices") }
    Tk.PropertyRow { label: qsTr("Number prefix"); Tk.TextField { id: prefixField; mono: true; placeholderText: "INV-" } }
    Tk.PropertyRow { label: qsTr("Next number"); Tk.TextField { id: nextField; mono: true } }
    Tk.PropertyRow { label: qsTr("Default terms"); Tk.ComboBox { id: termsBox; small: true; textRole: "name"; model: page.terms } }
    Tk.PropertyRow { label: qsTr("Note"); alignTop: true; Tk.TextArea { id: notesField; rows: 2; placeholderText: qsTr("Printed on every invoice") } }
    Tk.PropertyRow { label: qsTr("How to pay"); alignTop: true; Tk.TextArea { id: paymentField; rows: 3; placeholderText: qsTr("Bank transfer details, a payment link…") } }
    Tk.PropertyRow { label: qsTr("Accent colour"); Tk.ColorButton { id: accent; objectName: "prefAccent"; tooltip: qsTr("Accent of printed invoices and statements"); onColorSelected: function(c) { accent.color = c } } }
}
