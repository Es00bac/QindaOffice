// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// A general journal entry (debits = credits), a transfer between two bank
// accounts, or a deposit of undeposited payments (D-045/D-047). The
// service checks the balance exactly; the difference shown here is its
// answer, not a sum computed in QML.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string mode: "journal"      // journal | transfer | deposit
    property var lines: []
    property var accounts: []
    property var bankAccounts: []
    property var undeposited: []
    property var selectedPayments: ({})
    property var check: ({})

    objectName: "journalEntryDialog"
    title: mode === "transfer" ? qsTr("Transfer") : mode === "deposit" ? qsTr("Bank deposit") : qsTr("Journal entry")
    dialogWidth: Tk.Theme.size.dialogWidth * (mode === "journal" ? 2 : 1.3)
    primaryText: mode === "journal" ? qsTr("Post entry") : mode === "transfer" ? qsTr("Transfer") : qsTr("Deposit")
    secondaryText: qsTr("Cancel")
    primaryEnabled: mode !== "journal" || check.balanced === true

    function blank() { return { accountId: 0, debit: "", credit: "", memo: "" } }
    function openFor(m) {
        mode = m
        accounts = [{ id: 0, fullName: qsTr("Choose an account") }].concat(books.accounts(false))
        bankAccounts = books.bankAccounts()
        undeposited = mode === "deposit" ? books.undepositedPayments() : []
        selectedPayments = ({})
        lines = [blank(), blank()]
        dateField.text = books.today; memoField.text = ""; amountField.text = ""
        error.visible = false
        refreshCheck()
        open()
    }
    function setLine(i, field, value) { const next = lines.slice(); next[i] = Object.assign({}, next[i], { [field]: value }); lines = next; refreshCheck() }
    function refreshCheck() {
        if (mode !== "journal") return
        check = books.checkJournalEntry({ lines: lines.filter(function(l) { return l.accountId > 0 }) })
    }
    onAccepted: {
        let r
        if (mode === "journal")
            r = books.postJournalEntry({ date: dateField.text, memo: memoField.text, lines: lines.filter(function(l) { return l.accountId > 0 }) })
        else if (mode === "transfer")
            r = books.transfer({ date: dateField.text, memo: memoField.text, amount: amountField.text,
                                 fromAccountId: bankAccounts[fromBox.currentIndex].id, toAccountId: bankAccounts[toBox.currentIndex].id })
        else
            r = books.deposit({ date: dateField.text, memo: memoField.text, accountId: bankAccounts[toBox.currentIndex].id,
                                paymentIds: Object.keys(selectedPayments).filter(function(k) { return dialog.selectedPayments[k] }).map(Number) })
        if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not posted") }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.md
            Tk.PropertyRow { label: qsTr("Date"); Tk.TextField { id: dateField; objectName: "journalDate"; mono: true; small: true } }
            Tk.PropertyRow { Tk.Flex.grow: 1; label: qsTr("Memo"); Tk.TextField { id: memoField; small: true } }
        }
        // ---- transfer / deposit ----
        Tk.PropertyRow { visible: dialog.mode === "transfer"; label: qsTr("From"); Tk.ComboBox { id: fromBox; small: true; textRole: "name"; model: dialog.bankAccounts } }
        Tk.PropertyRow { visible: dialog.mode !== "journal"; label: dialog.mode === "deposit" ? qsTr("Deposit to") : qsTr("To"); Tk.ComboBox { id: toBox; small: true; textRole: "name"; model: dialog.bankAccounts; currentIndex: dialog.mode === "transfer" && dialog.bankAccounts.length > 1 ? 1 : 0 } }
        Tk.PropertyRow { visible: dialog.mode === "transfer"; label: qsTr("Amount"); Tk.TextField { id: amountField; mono: true; placeholderText: "0.00" } }
        Repeater {
            model: dialog.mode === "deposit" ? dialog.undeposited : []
            delegate: Tk.CheckBox {
                required property var modelData
                text: String(modelData.date) + " · " + String(modelData.partyName || "") + " · " + String(modelData.amountText || "")
                checked: dialog.selectedPayments[modelData.id] === true
                onToggled: { const next = Object.assign({}, dialog.selectedPayments); next[modelData.id] = checked; dialog.selectedPayments = next }
            }
        }
        Tk.Caption { visible: dialog.mode === "deposit" && dialog.undeposited.length === 0; text: qsTr("No payments are waiting to be deposited.") }
        // ---- journal lines ----
        Tk.Flex {
            visible: dialog.mode === "journal"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                Tk.Overline { title: qsTr("Account"); Tk.Flex.flex: 3 }
                Tk.Overline { title: qsTr("Debit"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0}
                Tk.Overline { title: qsTr("Credit"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0}
                Tk.Overline { title: qsTr("Memo"); Tk.Flex.flex: 2 }
            }
            Repeater {
                model: dialog.lines.length
                delegate: Tk.Flex {
                    id: jl
                    required property int index
                    readonly property var lineData: dialog.lines[index] || ({})
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                    Tk.ComboBox { Tk.Flex.flex: 3; small: true; textRole: "fullName"; model: dialog.accounts
                                  currentIndex: Math.max(0, dialog.accounts.map(function(a) { return a.id }).indexOf(jl.lineData.accountId || 0))
                                  onActivated: function(i) { dialog.setLine(jl.index, "accountId", dialog.accounts[i].id) } }
                    Tk.TextField { implicitWidth: Tk.Theme.size.tableColumn * 1.2; small: true; mono: true; horizontalAlignment: Text.AlignRight; text: String(jl.lineData.debit || "")
                                   onEditingFinished: dialog.setLine(jl.index, "debit", text) }
                    Tk.TextField { implicitWidth: Tk.Theme.size.tableColumn * 1.2; small: true; mono: true; horizontalAlignment: Text.AlignRight; text: String(jl.lineData.credit || "")
                                   onEditingFinished: dialog.setLine(jl.index, "credit", text) }
                    Tk.TextField { Tk.Flex.flex: 2; small: true; text: String(jl.lineData.memo || ""); onEditingFinished: dialog.setLine(jl.index, "memo", text) }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.md
                Tk.Button { small: true; variant: "ghost"; iconName: "plus"; text: qsTr("Add a line"); onClicked: dialog.lines = dialog.lines.concat([dialog.blank()]) }
                Tk.Spacer { }
                Tk.KeyValue { key: qsTr("Debits"); value: String(dialog.check.debitText || "0.00") }
                Tk.KeyValue { key: qsTr("Credits"); value: String(dialog.check.creditText || "0.00") }
                Tk.Badge { text: dialog.check.balanced ? qsTr("Balanced") : qsTr("Off by %1").arg(dialog.check.differenceText || "—"); variant: dialog.check.balanced ? "success" : "warning" }
            }
        }
    }
}
