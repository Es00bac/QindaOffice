// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Print Checks (M68, D-077), as QuickBooks runs it: the checks and bill
// payments waiting to print from one bank account; the first check number
// (the account's next) — the chosen checks take consecutive numbers as they
// go to the printer, through the "checks" print source (one painter, the
// printer's alignment). Then the confirmation: which printed correctly? A
// misprint goes back to the queue and its number is recorded void;
// "Nothing printed" takes the whole run back with its numbers unused.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property string stage: "queue"    // queue | confirm
    property var banks: []
    property int accountIndex: 0
    property var rows: []             // the queue of the chosen account
    property var picked: ({})         // id -> true
    property string firstNumber: ""
    property var printed: []          // the run: [{id, number, payeeName, amountText}]
    property var reprint: ({})        // id -> true (misprinted)
    readonly property int accountId: banks.length > 0 ? Number(banks[Math.min(accountIndex, banks.length - 1)].id) : 0

    objectName: "printChecksDialog"
    title: stage === "queue" ? qsTr("Print checks") : qsTr("Did the checks print correctly?")
    subtitle: stage === "queue" ? (banks.length > 0 ? String(banks[Math.min(accountIndex, banks.length - 1)].name) : "") : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.8
    primaryText: stage === "queue" ? qsTr("Print") : qsTr("Done")
    secondaryText: stage === "queue" ? qsTr("Cancel") : ""
    tertiaryText: stage === "confirm" ? qsTr("Nothing printed") : ""
    primaryEnabled: stage === "confirm" || Object.keys(picked).length > 0

    function openDialog() {
        banks = books.bankAccounts().filter(function(a) { return a.type === "Bank" })
        // The first account with checks waiting.
        accountIndex = Math.max(0, banks.findIndex(function(a) { return books.checkQueue(a.id).length > 0 }))
        stage = "queue"
        refresh()
        open()
    }
    function refresh() {
        rows = accountId > 0 ? books.checkQueue(accountId) : []
        const all = ({})
        rows.forEach(function(r) { all[r.id] = true })
        picked = all
        firstNumber = accountId > 0 ? String(books.nextCheckNumber(accountId)) : ""
        error.visible = false
    }
    function toggle(map, id, on) { const next = Object.assign({}, map); if (on) next[id] = true; else delete next[id]; return next }
    // Numbers the chosen checks and sends them to the printer.
    function printSelected() {
        const ids = rows.filter(function(r) { return dialog.picked[r.id] === true }).map(function(r) { return r.id })
        const result = books.assignCheckNumbers(accountId, ids, firstNumber)
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; return false }
        printed = result.checks
        reprint = ({})
        stage = "confirm"
        host.setPrintSource("checks", ids, ({}))
        return true
    }
    function finish() {
        const result = books.confirmCheckPrint(Object.keys(reprint).map(Number))
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; return false }
        stage = "queue"
        refresh()
        return rows.length === 0
    }
    function nothingPrinted() {
        const result = books.takeBackChecks(accountId, printed.map(function(c) { return c.id }))
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; return }
        stage = "queue"
        refresh()
    }
    onAccepted: {
        if (stage === "queue") {
            if (printSelected()) { open(); host.actions.trigger("filePrintAction") } else open()
        } else if (!finish()) {
            open() // the reprints wait in the queue, with the next numbers
        }
    }
    onTertiary: nothingPrinted()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not printed") }

        // ---- the queue ----
        Tk.Grid {
            visible: dialog.stage === "queue"
            columns: "2fr 1fr"
            columnGap: Tk.Theme.space.lg
            Tk.PropertyRow {
                label: qsTr("Bank account")
                Tk.ComboBox { objectName: "printChecksAccount"; small: true; textRole: "name"; model: dialog.banks; currentIndex: dialog.accountIndex
                              onActivated: function(i) { dialog.accountIndex = i; dialog.refresh() } }
            }
            Tk.PropertyRow {
                label: qsTr("First check number")
                Tk.TextField { objectName: "printChecksFirst"; small: true; mono: true; text: dialog.firstNumber; onEditingFinished: dialog.firstNumber = text.trim() }
            }
        }
        Tk.Caption {
            visible: dialog.stage === "queue"
            text: dialog.rows.length === 0 ? qsTr("No checks are waiting to print from this account. Save a check or a bill payment with Print later to add one.")
                                           : qsTr("Load the check stock so the first sheet is check %1; the checks take consecutive numbers.").arg(dialog.firstNumber)
            wrapMode: Text.Wrap
        }
        Repeater {
            model: dialog.stage === "queue" ? dialog.rows : []
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "queuedCheck_" + modelData.id
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.CheckBox {
                    checked: dialog.picked[row.modelData.id] === true
                    onToggled: { dialog.picked = dialog.toggle(dialog.picked, row.modelData.id, checked)
                                 checked = Qt.binding(function() { return dialog.picked[row.modelData.id] === true }) }
                }
                Tk.Mono { text: String(row.modelData.date); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0 }
                Tk.Label { text: String(row.modelData.payeeName); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Caption { text: String(row.modelData.kindLabel); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0 }
                Tk.Mono { text: String(row.modelData.amountText); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            }
        }
        Tk.Flex {
            visible: dialog.stage === "queue"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { Tk.Flex.grow: 1; text: qsTr("Each printer has its own alignment; the checks print where it says."); wrapMode: Text.Wrap }
            Tk.Button { objectName: "printChecksAlign"; small: true; variant: "ghost"; iconName: "crosshair"; text: qsTr("Check printer alignment…")
                        onClicked: dialog.host.actions.trigger("booksCheckAlignmentAction") }
        }

        // ---- the confirmation ----
        Tk.Caption {
            visible: dialog.stage === "confirm"
            text: qsTr("If every check printed correctly, choose Done. Tick any that jammed or printed wrong: they go back to the queue and their numbers are recorded void.")
            wrapMode: Text.Wrap
        }
        Repeater {
            model: dialog.stage === "confirm" ? dialog.printed : []
            delegate: Tk.Flex {
                id: done
                required property var modelData
                objectName: "printedCheck_" + modelData.id
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Mono { text: qsTr("No. %1").arg(done.modelData.number); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0 }
                Tk.Label { text: String(done.modelData.payeeName); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Mono { text: String(done.modelData.amountText); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.CheckBox {
                    objectName: "reprintCheck"
                    text: qsTr("Reprint")
                    checked: dialog.reprint[done.modelData.id] === true
                    onToggled: { dialog.reprint = dialog.toggle(dialog.reprint, done.modelData.id, checked)
                                 checked = Qt.binding(function() { return dialog.reprint[done.modelData.id] === true }) }
                }
            }
        }
    }
}
