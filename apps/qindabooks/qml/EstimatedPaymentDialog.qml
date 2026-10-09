// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Estimated taxes ▸ Record payment (M71, D-081): a federal (Form 1040-ES)
// or Colorado (DR 0104EP) estimated-tax payment the owner made through
// EFTPS, IRS Direct Pay or Colorado's Revenue Online. The tax is the
// owner's, not the business's, so it posts as an owner's draw marked as
// that year's installment (OwnerPay, owner_pay.h); the planner counts it
// against the installments. QindaBooks never pays the IRS or Colorado.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property PayrollService payroll: null
    property var accounts: []
    property var owners: []
    property string problem: ""
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var jurisdictions: [{ text: qsTr("Federal (Form 1040-ES)"), value: "federal" },
                                          { text: qsTr("Colorado (DR 0104EP)"), value: "colorado" }]

    objectName: "estimatedPaymentDialog"
    title: qsTr("Record an estimated tax payment")
    subtitle: qsTr("Paid through EFTPS, IRS Direct Pay or Revenue Online; recorded here as an owner's draw")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: qsTr("Record payment")
    secondaryText: qsTr("Cancel")
    primaryEnabled: accounts.length > 0

    function openFor(jurisdiction, installment, amountPlain, year) {
        accounts = books.bankAccounts()
        owners = [{ id: 0, name: qsTr("(no owner named)") }].concat(payroll.owners())
        const setup = payroll.setup()
        let bank = 0
        for (let i = 0; i < accounts.length; ++i) if (Number(accounts[i].id) === Number(setup.payrollBankAccountId || 0)) bank = i
        bankBox.currentIndex = bank
        ownerBox.currentIndex = owners.length > 1 ? 1 : 0
        whereBox.currentIndex = jurisdiction === "colorado" ? 1 : 0
        installmentBox.currentIndex = Math.max(0, Math.min(3, Number(installment || 1) - 1))
        yearField.text = String(year || books.today.substring(0, 4))
        amountField.text = String(amountPlain || "")
        dateField.text = books.today
        referenceField.text = ""
        problem = ""
        open()
    }
    // The record step; the tests fill the fields and call it directly.
    function record() {
        const r = payroll.recordEstimatedPayment({ jurisdiction: jurisdictions[whereBox.currentIndex].value, taxYear: Number(yearField.text),
                                                   installment: installmentBox.currentIndex + 1, amount: amountField.text, date: dateField.text,
                                                   bankAccountId: accounts.length > 0 ? accounts[bankBox.currentIndex].id : 0,
                                                   ownerId: owners[Math.max(0, ownerBox.currentIndex)].id, reference: referenceField.text })
        problem = r.ok ? "" : String(r.error || "")
        return r
    }
    onAccepted: { if (!record().ok) open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "estimatedPaymentProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not recorded"); text: dialog.problem }
        Tk.Segmented { id: whereBox; objectName: "estimatedPaymentWhere"; small: true; model: dialog.jurisdictions }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Tax year"); Tk.TextField { id: yearField; objectName: "estimatedPaymentYear"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("Payment")
                Tk.Segmented { id: installmentBox; objectName: "estimatedPaymentInstallment"; small: true; model: ["1", "2", "3", "4"] }
            }
            Tk.PropertyRow {
                label: qsTr("Amount")
                Tk.TextField { id: amountField; objectName: "estimatedPaymentAmount"; small: true; mono: true; horizontalAlignment: Text.AlignRight }
            }
            Tk.PropertyRow { label: qsTr("Date paid"); Tk.TextField { id: dateField; objectName: "estimatedPaymentDate"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("From")
                Tk.ComboBox { id: bankBox; objectName: "estimatedPaymentBank"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow {
                label: qsTr("Owner")
                Tk.ComboBox { id: ownerBox; objectName: "estimatedPaymentOwner"; small: true; textRole: "name"; model: dialog.owners }
            }
            Tk.PropertyRow {
                label: qsTr("Confirmation")
                Tk.TextField { id: referenceField; objectName: "estimatedPaymentReference"; small: true; mono: true; placeholderText: qsTr("EFTPS or Revenue Online number") }
            }
        }
    }
}
