// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Pay Owner… / Shareholder Distribution… / Pay Dividend… (M71,
// D-081): money a business pays its owners, the way its type allows — a
// sole proprietor's or partner's draw, an S corporation's shareholder
// distribution, a C corporation's dividend (from retained earnings); a
// nonprofit has no owners to pay — by check (numbered now, or queued for
// Checks to Print and its confirm step, M68) or by transfer. It is equity,
// never an expense. After a draw the dialog shows the share to set aside
// for taxes (the planner's rate on this year's profit,
// PayrollService::setAsideFor) and moves it to the tax savings account on
// request. A corporation's owners' wages are paychecks (Run Payroll),
// never this dialog.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property PayrollService payroll: null
    property string kind: "draw"               // "draw" | "distribution" | "dividend"; "" = none (a nonprofit)
    property string requested: "draw"
    property var owners: []
    property var accounts: []
    property var setupRecord: ({})
    property var hint: ({})                    // setAsideFor(amount) before recording
    property var saved: null                   // payOwner's result once recorded
    property bool moved: false
    property string problem: ""
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var ownerChoices: owners.concat([{ id: -1, name: qsTr("New owner…") }])
    readonly property bool newOwner: ownerBox.currentIndex >= owners.length
    readonly property bool byCheck: methodBox.currentIndex === 0
    readonly property string asideName: {
        for (const a of accounts) if (Number(a.id) === Number(setupRecord.setAsideAccountId || 0)) return String(a.name)
        return ""
    }
    readonly property bool offersSetAside: saved !== null && kind === "draw" && Number(saved.setAside || 0) > 0 && !moved

    objectName: "payOwnerDialog"
    title: kind === "distribution" ? qsTr("Shareholder distribution") : kind === "dividend" ? qsTr("Pay a dividend") : qsTr("Pay owner")
    subtitle: kind === "distribution" ? qsTr("Profit paid out to the shareholders, after the owners' reasonable salaries")
            : kind === "dividend" ? qsTr("A C corporation's profit paid to its shareholders, from retained earnings")
            : kind === "draw" ? qsTr("An owner's draw: money out of the business, not an expense")
            : qsTr("A nonprofit has no owners to pay")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: saved !== null ? qsTr("Done") : qsTr("Record")
    secondaryText: saved !== null ? "" : qsTr("Cancel")
    tertiaryText: offersSetAside && asideName.length > 0 ? qsTr("Move %1 to %2").arg(saved.setAsideText).arg(asideName) : ""
    primaryEnabled: saved !== null || (accounts.length > 0 && kind.length > 0)

    // What each business type pays its owners (owner_pay.h).
    function allowedFor(entity) {
        return entity === "scheduleC" || entity === "partnership" ? ["draw"]
             : entity === "sCorporation" ? ["distribution"]
             : entity === "cCorporation" ? ["dividend"]
             : entity === "nonprofit" ? [] : ["draw", "distribution", "dividend"]
    }
    // A kind the business type does not pay becomes the one it does,
    // saying so.
    function openFor(asked) {
        requested = asked === "distribution" || asked === "dividend" ? asked : "draw"
        const allowed = allowedFor(payroll.entity)
        kind = allowed.indexOf(requested) >= 0 ? requested : (allowed.length > 0 ? allowed[0] : "")
        owners = payroll.owners()
        accounts = books.bankAccounts()
        setupRecord = payroll.setup()
        ownerBox.currentIndex = 0
        let bank = 0
        for (let i = 0; i < accounts.length; ++i) if (Number(accounts[i].id) === Number(setupRecord.payrollBankAccountId || 0)) bank = i
        bankBox.currentIndex = bank
        ownerName.text = ""; ownerAddress.text = ""
        amountField.text = ""; dateField.text = books.today
        methodBox.currentIndex = 1; printLater.checked = true; numberField.text = ""; memoField.text = ""
        hint = ({}); saved = null; moved = false; problem = ""
        open()
    }
    function ownerId() {
        if (!newOwner) return owners.length > 0 ? owners[ownerBox.currentIndex].id : 0
        const o = payroll.saveOwner({ name: ownerName.text, address: ownerAddress.text })
        if (!o.ok) { problem = String(o.error || ""); return -1 }
        owners = payroll.owners()
        for (let i = 0; i < owners.length; ++i) if (Number(owners[i].id) === Number(o.id)) ownerBox.currentIndex = i
        return o.id
    }
    // The record step; the tests fill the fields and call it directly.
    function record() {
        const owner = ownerId()
        if (owner < 0) return { ok: false, error: problem }
        const r = payroll.payOwner({ kind: kind, ownerId: owner, bankAccountId: accounts.length > 0 ? accounts[bankBox.currentIndex].id : 0,
                                     amount: amountField.text, date: dateField.text, method: byCheck ? "check" : "transfer",
                                     checkNumber: numberField.text, toPrint: byCheck && printLater.checked, memo: memoField.text })
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) saved = r
        return r
    }
    function moveSetAside() {
        const r = payroll.recordSetAside({ amount: saved.setAsidePlain, date: dateField.text,
                                           bankAccountId: accounts[bankBox.currentIndex].id, toAccountId: setupRecord.setAsideAccountId })
        problem = r.ok ? "" : String(r.error || "")
        moved = r.ok
        return r
    }
    // Record, then stay open on a refusal or to offer the set-aside; Done closes.
    onAccepted: { if (saved === null && (!record().ok || offersSetAside)) open() }
    onTertiary: { if (moveSetAside().ok) close() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "payOwnerProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not recorded"); text: dialog.problem }
        Tk.Notice {
            objectName: "payOwnerSwitched"
            visible: dialog.kind !== dialog.requested
            variant: "info"
            title: dialog.kind === "distribution" ? qsTr("An S corporation pays distributions")
                 : dialog.kind === "dividend" ? qsTr("A C corporation pays dividends")
                 : dialog.kind === "draw" ? qsTr("This business's owners take draws") : qsTr("No owners to pay")
            text: dialog.kind === "distribution"
                  ? qsTr("The business is taxed as an S corporation: its owners are paid a salary through Run Payroll, and profit beyond it as shareholder distributions. This records a distribution.")
                  : dialog.kind === "dividend"
                  ? qsTr("The business is a C corporation: its owners are paid a salary through Run Payroll, and profit paid to shareholders is a dividend from retained earnings. This records a dividend.")
                  : dialog.kind === "draw"
                  ? qsTr("A sole proprietor or partner is not paid wages, distributions or dividends: money out of the business is an owner's draw. This records a draw.")
                  : qsTr("The business is a nonprofit: it has no owners, and the people who work for it are paid through Run Payroll.")
        }
        Tk.Notice {
            objectName: "payOwnerSaved"
            visible: dialog.saved !== null
            variant: "success"
            title: dialog.kind === "distribution" ? qsTr("Distribution recorded") : dialog.kind === "dividend" ? qsTr("Dividend recorded")
                 : qsTr("Draw recorded")
            text: dialog.byCheck && printLater.checked ? qsTr("The check waits in Checks to Print (Purchases).") : qsTr("It is in the register.")
        }
        Tk.Notice {
            objectName: "payOwnerSetAside"
            visible: dialog.saved !== null && dialog.kind === "draw" && Number(dialog.saved.setAside || 0) > 0
            variant: dialog.moved ? "success" : "warning"
            title: dialog.moved ? qsTr("Set aside") : qsTr("Set aside %1 for taxes").arg(dialog.saved !== null ? dialog.saved.setAsideText : "")
            text: dialog.moved ? qsTr("Moved to %1.").arg(dialog.asideName)
                : qsTr("At this year's profit, the owner's income tax and self-employment tax take about %1% of what the business makes. %2")
                      .arg(dialog.saved !== null ? dialog.saved.setAsidePercent : 0)
                      .arg(dialog.asideName.length > 0 ? qsTr("Move it to %1 so the estimated payments are there when they fall due.").arg(dialog.asideName)
                                                       : qsTr("Choose a tax savings account in Set Up Owner Pay & Payroll to move it there."))
        }
        Tk.Grid {
            visible: dialog.saved === null && dialog.kind.length > 0
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Owner")
                Tk.ComboBox { id: ownerBox; objectName: "payOwnerOwner"; small: true; textRole: "name"; model: dialog.ownerChoices }
            }
            Tk.PropertyRow {
                label: qsTr("From")
                Tk.ComboBox { id: bankBox; objectName: "payOwnerBank"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow { visible: dialog.newOwner; label: qsTr("Owner's name"); Tk.TextField { id: ownerName; objectName: "payOwnerName"; small: true } }
            Tk.PropertyRow {
                visible: dialog.newOwner; label: qsTr("Mailing address"); alignTop: true
                Tk.TextArea { id: ownerAddress; objectName: "payOwnerAddress"; rows: 2 }
            }
            Tk.PropertyRow {
                label: qsTr("Amount")
                Tk.TextField {
                    id: amountField; objectName: "payOwnerAmount"; small: true; mono: true; horizontalAlignment: Text.AlignRight
                    onEditingFinished: if (dialog.kind === "draw" && text.trim().length > 0) dialog.hint = dialog.payroll.setAsideFor(text)
                }
            }
            Tk.PropertyRow { label: qsTr("Date"); Tk.TextField { id: dateField; objectName: "payOwnerDate"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("Paid by")
                Tk.Segmented { id: methodBox; objectName: "payOwnerMethod"; small: true; model: [qsTr("Check"), qsTr("Transfer")] }
            }
            Tk.PropertyRow {
                visible: dialog.byCheck; label: qsTr("Print later")
                hint: qsTr("Queue it for Checks to Print, which numbers it.")
                Tk.Switch { id: printLater; objectName: "payOwnerPrintLater"; small: true }
            }
            Tk.PropertyRow {
                visible: dialog.byCheck && !printLater.checked; label: qsTr("Check number")
                Tk.TextField { id: numberField; objectName: "payOwnerNumber"; small: true; mono: true }
            }
            Tk.PropertyRow { label: qsTr("Memo"); Tk.TextField { id: memoField; objectName: "payOwnerMemo"; small: true; placeholderText: dialog.kind === "distribution" ? qsTr("Shareholder distribution") : dialog.kind === "dividend" ? qsTr("Dividend")
                                                           : qsTr("Owner's draw") } }
        }
        Tk.Caption {
            objectName: "payOwnerHint"
            visible: dialog.saved === null && Number(dialog.hint.percent || 0) > 0
            wrapMode: Text.Wrap
            text: qsTr("Of this draw, about %1 (%2%) is this year's tax on the profit it comes from — the planner under Estimated taxes.")
                      .arg(dialog.hint.amountText || "").arg(dialog.hint.percent || 0)
        }
    }
}
