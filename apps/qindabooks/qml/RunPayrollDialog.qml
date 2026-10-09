// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Run Payroll… (M71, D-081), QuickBooks' three steps: enter the
// hours and any bonus for each employee (the grid), preview every
// paycheck — gross, each tax withheld, deductions, net pay and the
// employer's taxes, computed by books_payroll from the official tables —
// then create them all in one transaction. "Print later" queues the
// paychecks for Checks to Print (M68), where they get their numbers and
// the confirm step; otherwise each takes the number typed (or none, for a
// direct deposit made at the bank).
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property PayrollService payroll: null
    property string stage: "enter"          // enter | preview | done
    property var rows: []                   // the grid, edited in place
    property var accounts: []
    property var preview: ({})
    property int created: 0
    property string problem: ""
    readonly property bool narrow: width < Tk.Theme.size.dialogWidth * 1.5

    objectName: "runPayrollDialog"
    title: qsTr("Run payroll")
    subtitle: stage === "enter" ? qsTr("Step 1 of 3: hours and extra pay")
            : stage === "preview" ? qsTr("Step 2 of 3: check every paycheck") : qsTr("Step 3 of 3: paychecks created")
    dialogWidth: Tk.Theme.size.dialogWidth * 2.6
    primaryText: stage === "enter" ? qsTr("Preview") : stage === "preview" ? qsTr("Create paychecks") : qsTr("Done")
    primaryEnabled: stage === "done" || (rows.length > 0 && accounts.length > 0 && (stage === "enter" || preview.ok === true))
    secondaryText: stage === "done" ? "" : qsTr("Cancel")
    tertiaryText: stage === "preview" ? qsTr("Back") : stage === "done" && printLater.checked ? qsTr("Print paychecks…") : ""

    function openDialog() {
        const draft = payroll.payrollDraft(books.today)
        rows = (draft.rows || []).map(function(r) { return Object.assign({}, r) })
        accounts = books.bankAccounts()
        let bank = 0
        for (let i = 0; i < accounts.length; ++i) if (Number(accounts[i].id) === Number(draft.bankAccountId || 0)) bank = i
        bankBox.currentIndex = bank
        payDate.text = String(draft.payDate || books.today)
        periodStart.text = String(draft.periodStart || ""); periodEnd.text = String(draft.periodEnd || "")
        printLater.checked = true
        stage = "enter"; preview = ({}); created = 0; problem = ""
        open()
    }
    function edit(index, key, value) { rows[index][key] = value }
    function run() {
        return { payDate: payDate.text, periodStart: periodStart.text, periodEnd: periodEnd.text,
                 bankAccountId: accounts.length > 0 ? accounts[bankBox.currentIndex].id : 0, toPrint: printLater.checked, rows: rows }
    }
    // The steps; the tests fill the grid and call them directly.
    function previewRun() {
        preview = payroll.previewPayroll(run())
        problem = preview.ok ? "" : String(preview.error || "")
        stage = "preview"
        return preview
    }
    function create() {
        const r = payroll.runPayroll(run())
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) { created = (r.ids || []).length; stage = "done" }
        return r
    }
    onAccepted: {
        if (stage === "enter") { previewRun(); open() }
        else if (stage === "preview") { create(); open() }
    }
    onTertiary: {
        if (stage === "preview") { stage = "enter"; problem = "" }
        else { close(); host.actions.trigger("booksPrintChecksAction") }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "runPayrollProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not ready"); text: dialog.problem }
        Tk.EmptyState {
            visible: dialog.rows.length === 0
            iconName: "users"
            title: qsTr("Nobody to pay yet")
            text: qsTr("Set up an employee's pay and withholding under Payroll ▸ Employee Payroll first.")
        }
        Tk.Grid {
            visible: dialog.stage === "enter" && dialog.rows.length > 0
            columns: dialog.narrow ? "1fr" : "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Pay date"); Tk.TextField { id: payDate; objectName: "runPayrollDate"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("Period from"); Tk.TextField { id: periodStart; objectName: "runPayrollFrom"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("to"); Tk.TextField { id: periodEnd; objectName: "runPayrollTo"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("From"); Tk.ComboBox { id: bankBox; objectName: "runPayrollBank"; small: true; textRole: "name"; model: dialog.accounts } }
            Tk.PropertyRow {
                label: qsTr("Print later"); hint: qsTr("Queue the paychecks for Checks to Print.")
                Tk.Switch { id: printLater; objectName: "runPayrollPrintLater"; small: true }
            }
        }
        Tk.Flex {
            visible: dialog.stage === "enter" && dialog.rows.length > 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Employee"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Overline { title: qsTr("Regular pay"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.4; Tk.Flex.shrink: 0 }
            Tk.Overline { title: qsTr("Hours"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0 }
            Tk.Overline { title: qsTr("Bonus"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0 }
            Tk.Overline { visible: !printLater.checked; title: qsTr("Check no."); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0 }
        }
        Repeater {
            model: dialog.stage === "enter" ? dialog.rows : []
            delegate: Tk.Flex {
                id: line
                required property var modelData
                required property int index
                objectName: "runPayrollRow_" + index
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label { text: String(line.modelData.name); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Mono { text: String(line.modelData.payText); horizontalAlignment: Text.AlignRight; Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.4; Tk.Flex.shrink: 0 }
                Tk.TextField {
                    objectName: "runPayrollHours_" + line.index
                    small: true; mono: true; enabled: line.modelData.hourly === true; text: String(line.modelData.hours || "")
                    placeholderText: line.modelData.hourly === true ? "0" : qsTr("salary")
                    Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0
                    onTextEdited: dialog.edit(line.index, "hours", text)
                }
                Tk.TextField {
                    objectName: "runPayrollBonus_" + line.index
                    small: true; mono: true; horizontalAlignment: Text.AlignRight; text: String(line.modelData.bonus || ""); placeholderText: "0.00"
                    Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0
                    onTextEdited: dialog.edit(line.index, "bonus", text)
                }
                Tk.TextField {
                    objectName: "runPayrollNumber_" + line.index
                    visible: !printLater.checked
                    small: true; mono: true; text: String(line.modelData.checkNumber || "")
                    Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0
                    onTextEdited: dialog.edit(line.index, "checkNumber", text)
                }
            }
        }
        // Narrow, the columns keep their minimum widths and scroll sideways.
        Tk.DataTable {
            objectName: "runPayrollPreview"
            visible: dialog.stage === "preview"
            implicitHeight: Tk.Theme.size.row * (Math.min(8, (dialog.preview.rows || []).length) + 1.5)
            model: dialog.stage === "preview" ? (dialog.preview.rows || []) : []
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Employee"); flex: 1.6; minWidth: Tk.Theme.size.tableColumn * 1.4 },
                Tk.TableColumn { key: "grossText"; title: qsTr("Gross"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "fitText"; title: qsTr("Federal"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "ssText"; title: qsTr("Soc. sec."); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "medicareText"; title: qsTr("Medicare"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "coloradoText"; title: qsTr("Colorado"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "famliText"; title: qsTr("FAMLI"); flex: 0.8; minWidth: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "optText"; title: qsTr("OPT"); flex: 0.7; minWidth: Tk.Theme.size.tableColumn * 0.7; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "deductionsText"; title: qsTr("Deductions"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "netText"; title: qsTr("Net pay"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "employerText"; title: qsTr("Employer"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true }
            ]
        }
        Tk.Grid {
            objectName: "runPayrollTotals"
            visible: dialog.stage !== "enter" && dialog.preview.ok === true
            columns: dialog.narrow ? "1fr 1fr" : "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.lg
            Tk.KeyValue { key: qsTr("Gross pay"); value: String((dialog.preview.totals || {}).grossText || "") }
            Tk.KeyValue { key: qsTr("Taxes withheld"); value: String((dialog.preview.totals || {}).withheldText || "") }
            Tk.KeyValue { key: qsTr("Deductions"); value: String((dialog.preview.totals || {}).deductionsText || "") }
            Tk.KeyValue { key: qsTr("Net pay"); value: String((dialog.preview.totals || {}).netText || ""); accent: true }
            Tk.KeyValue { key: qsTr("Employer taxes"); value: String((dialog.preview.totals || {}).employerText || "") }
            Tk.KeyValue { key: qsTr("Total cost"); value: String((dialog.preview.totals || {}).costText || "") }
        }
        Repeater {
            model: dialog.stage === "preview" ? (dialog.preview.rows || []) : []
            delegate: Tk.Caption {
                required property var modelData
                visible: modelData.ok !== true || (modelData.notes || []).length > 0
                wrapMode: Text.Wrap
                text: String(modelData.name) + ": " + (modelData.ok !== true ? String(modelData.error || "") : (modelData.notes || []).join(" "))
                color: modelData.ok !== true ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            }
        }
        Tk.Notice {
            objectName: "runPayrollDone"
            visible: dialog.stage === "done"
            variant: "success"
            title: qsTr("%1 paychecks created").arg(dialog.created)
            text: (printLater.checked ? qsTr("They wait in Checks to Print; print them on voucher checks with the pay stub. ")
                                      : qsTr("They are in the register. "))
                  + qsTr("The taxes withheld and the employer's taxes are now payroll liabilities: pay them through EFTPS, Revenue Online, MyUI Employer+ and My FAMLI+ Employer, then record each payment under Pay Payroll Liabilities.")
        }
    }
}
