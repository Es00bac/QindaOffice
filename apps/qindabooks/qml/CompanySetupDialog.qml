// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// First run for a company (D-045/D-047): who the business is, its currency
// and fiscal year, a starter chart of accounts (general, services,
// nonprofit, creative studio), and where the company file lives. Each
// business gets its own file — records of different businesses never mix.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property int step: 0
    property var templates: []
    property var currencies: []
    property string templateId: ""
    readonly property var months: [qsTr("January"), qsTr("February"), qsTr("March"), qsTr("April"), qsTr("May"), qsTr("June"),
                                   qsTr("July"), qsTr("August"), qsTr("September"), qsTr("October"), qsTr("November"), qsTr("December")]

    objectName: "companySetupDialog"
    title: qsTr("Set up your company")
    subtitle: step === 0 ? qsTr("Step 1 of 2 · The business") : qsTr("Step 2 of 2 · Accounts")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: step === 0 ? qsTr("Next") : qsTr("Create company file…")
    secondaryText: qsTr("Cancel")
    tertiaryText: step === 1 ? qsTr("Back") : ""
    primaryEnabled: step === 0 ? nameField.text.trim().length > 0 : templateId.length > 0

    onAboutToShow: {
        if (books === null) return
        templates = books.templates()
        currencies = books.currencies()
        if (templateId.length === 0 && templates.length > 0) templateId = templates[0].id
        step = 0
    }
    onAccepted: {
        if (step === 0) { step = 1; open(); return }
        fileDialog.selectedFile = "file://" + (nameField.text.trim().replace(/[^A-Za-z0-9 _-]/g, "") || "Company") + ".qbooks"
        fileDialog.open()
    }
    onTertiary: { step = 0; open() }

    function settings() {
        return { name: nameField.text.trim(), legalName: legalField.text.trim(), address: addressField.text,
                 email: emailField.text.trim(), phone: phoneField.text.trim(), taxId: taxIdField.text.trim(),
                 currency: dialog.currencies.length ? dialog.currencies[currencyBox.currentIndex].code : "USD",
                 fiscalYearStartMonth: fiscalBox.currentIndex + 1, basis: basisBox.currentIndex === 1 ? "cash" : "accrual" }
    }
    Dialogs.FileDialog {
        id: fileDialog
        title: qsTr("Save the company file")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("QindaBooks company (*.qbooks)")]
        onAccepted: {
            const r = dialog.books.createCompany(selectedFile, dialog.settings(), dialog.templateId)
            if (!r.ok) { error.text = String(r.error || ""); error.visible = true; dialog.open() }
        }
        onRejected: dialog.open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("The company file was not created") }
        // ---- step 1 ----
        Tk.Flex {
            visible: dialog.step === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Business name"); Tk.TextField { id: nameField; objectName: "setupName"; placeholderText: qsTr("As your customers know it") } }
            Tk.PropertyRow { label: qsTr("Legal name"); Tk.TextField { id: legalField; objectName: "setupLegalName"; placeholderText: qsTr("If different") } }
            Tk.PropertyRow { label: qsTr("Address"); alignTop: true; Tk.TextArea { id: addressField; rows: 3 } }
            Tk.PropertyRow { label: qsTr("Email"); Tk.TextField { id: emailField } }
            Tk.PropertyRow { label: qsTr("Phone"); Tk.TextField { id: phoneField } }
            Tk.PropertyRow { label: qsTr("Tax ID"); Tk.TextField { id: taxIdField; placeholderText: qsTr("EIN or VAT number") } }
            Tk.PropertyRow { label: qsTr("Currency"); Tk.ComboBox { id: currencyBox; objectName: "setupCurrency"; small: true; textRole: "label"
                model: dialog.currencies.map(function(c) { return { code: c.code, label: c.code + " — " + c.name } }) } }
            Tk.PropertyRow { label: qsTr("Fiscal year starts"); Tk.ComboBox { id: fiscalBox; small: true; model: dialog.months } }
            Tk.PropertyRow { label: qsTr("Reports on"); hint: qsTr("Accrual counts income when invoiced; cash when paid. Every report can switch.")
                Tk.Segmented { id: basisBox; small: true; model: [qsTr("Accrual"), qsTr("Cash")] } }
        }
        // ---- step 2 ----
        Tk.Flex {
            visible: dialog.step === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Start from a chart of accounts that fits the business. You can add, rename and nest accounts later."); wrapMode: Text.Wrap; width: parent.width }
            Repeater {
                model: dialog.templates
                delegate: Tk.Card {
                    id: card
                    required property var modelData
                    objectName: "setupTemplate_" + modelData.id
                    interactive: true
                    selected: dialog.templateId === modelData.id
                    tooltip: String(modelData.name)
                    onClicked: dialog.templateId = modelData.id
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: String(card.modelData.name); font.weight: Font.DemiBold }
                        Tk.Caption { text: String(card.modelData.description || ""); wrapMode: Text.Wrap; width: parent.width }
                    }
                }
            }
        }
    }
}
