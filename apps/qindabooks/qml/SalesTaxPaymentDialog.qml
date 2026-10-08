// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Accounting ▸ Pay Sales Tax… (M63, D-071): what each tax agency is owed
// (collected minus paid, as of a date) and recording a payment to one of
// them from a bank or card account — Dr Sales Tax Payable for that agency,
// Cr the account (SalesTax::recordPayment). The amount starts at what is
// owed; any other amount is typed text the service validates exactly.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var agencies: []          // salesTaxOwed(asOf)
    property var accounts: []          // bankAccounts()
    property string asOf: ""
    property string problem: ""
    readonly property var agency: agencyBox.currentIndex >= 0 && agencyBox.currentIndex < agencies.length
                                  ? agencies[agencyBox.currentIndex] : null

    objectName: "salesTaxPaymentDialog"
    title: qsTr("Pay sales tax")
    subtitle: qsTr("Sales tax collected and not yet paid, by agency")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: qsTr("Record payment")
    primaryEnabled: agencies.length > 0 && accounts.length > 0
    secondaryText: qsTr("Cancel")

    function openDialog() {
        asOf = books.today
        accounts = books.bankAccounts()
        reload()
        dateField.text = books.today
        memoField.text = ""
        problem = ""
        open()
    }
    function reload() {
        agencies = books.salesTaxOwed(asOf)
        agencyBox.currentIndex = agencies.length > 0 ? Math.max(0, agencyBox.currentIndex) : -1
        amountField.text = agency !== null ? String(agency.owedPlain) : ""
    }
    // The record step; the tests fill the fields and call it directly.
    function record() {
        const r = books.recordSalesTaxPayment({ agencyId: agency !== null ? agency.agencyId : 0,
                                                bankAccountId: accounts.length > 0 ? accounts[accountBox.currentIndex].id : 0,
                                                amount: amountField.text, date: dateField.text, memo: memoField.text })
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) reload()
        return r
    }
    onAccepted: { if (!record().ok) open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "salesTaxProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not recorded"); text: dialog.problem }
        Tk.EmptyState {
            visible: dialog.agencies.length === 0
            iconName: "percent"
            title: qsTr("No sales tax agencies")
            text: qsTr("Add a sales tax rate (Accounting ▸ Sales tax) and the agency it is paid to appears here.")
        }
        Tk.Flex {
            visible: dialog.agencies.length > 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Owed as of") }
            Tk.TextField { objectName: "salesTaxAsOf"; small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: dialog.asOf
                           onEditingFinished: { dialog.asOf = text; dialog.reload() } }
        }
        Tk.DataTable {
            objectName: "salesTaxAgencies"
            visible: dialog.agencies.length > 0
            implicitHeight: Tk.Theme.size.panelMinHeight * 1.4
            model: dialog.agencies
            currentIndex: agencyBox.currentIndex
            onActivated: function(index) { agencyBox.currentIndex = index; dialog.reload() }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Agency"); flex: 1 },
                Tk.TableColumn { key: "owedText"; title: qsTr("Owed"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true }
            ]
        }
        Tk.Grid {
            visible: dialog.agencies.length > 0
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Pay")
                Tk.ComboBox { id: agencyBox; objectName: "salesTaxAgency"; small: true; textRole: "name"; model: dialog.agencies
                              onActivated: dialog.reload() }
            }
            Tk.PropertyRow { label: qsTr("Amount"); Tk.TextField { id: amountField; objectName: "salesTaxAmount"; small: true; mono: true; horizontalAlignment: Text.AlignRight } }
            Tk.PropertyRow {
                label: qsTr("From")
                Tk.ComboBox { id: accountBox; objectName: "salesTaxAccount"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow { label: qsTr("Date"); Tk.TextField { id: dateField; objectName: "salesTaxDate"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("Memo"); Tk.TextField { id: memoField; objectName: "salesTaxMemo"; small: true; placeholderText: qsTr("Return period, confirmation number…") } }
        }
    }
}
