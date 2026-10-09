// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll (M71, D-081): how the owner is paid and, with employees, the
// payroll — one workspace in tabs. Overview: how this LLC pays its owner,
// what falls due in the next weeks, and the copies saved for filing (W-2s
// and wage reports with full identity numbers, the SSA's EFW2 file).
// Owner pay: draws or distributions and tax set-asides. Estimated taxes:
// the planner. Employees, Paychecks, Liabilities: the payroll itself. The
// Payroll menu's dialogs (setup, Pay Owner, Run Payroll, Pay Liabilities)
// live in BooksWindow; the ones opened from a row live here.
Tk.Box {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property PayrollService payroll: null
    property int tab: 0
    property var upcoming: []
    property var ownerRows: []
    property int ownerPick: -1
    readonly property string entity: payroll !== null ? payroll.entity : "unsure"
    readonly property bool configured: payroll !== null && payroll.configured
    readonly property int year: books !== null && books.today.length >= 4 ? Number(books.today.substring(0, 4)) : 0
    readonly property bool narrow: width < Tk.Theme.size.panelMinWidth * 3
    readonly property var tabNames: [qsTr("Overview"), qsTr("Owner pay"), qsTr("Estimated taxes"), qsTr("Employees"),
                                     qsTr("Paychecks"), qsTr("Liabilities")]
    readonly property var entityLabels: ({ scheduleC: qsTr("Schedule C"), sCorporation: qsTr("S corporation"),
                                           partnership: qsTr("Partnership"), unsure: qsTr("Tax type not confirmed") })

    color: Tk.Theme.color.surface
    padding: 0

    function refresh() {
        if (books === null || !books.hasCompany || payroll === null) { upcoming = []; ownerRows = []; return }
        if (tab === 0) upcoming = payroll.upcoming(45)
        if (tab === 1) ownerRows = payroll.ownerPayments(year)
        if (tab === 2) estimates.refresh()
        if (tab >= 3 && tab <= 4) people.refresh()
        if (tab === 5) liabilities.refresh()
    }
    function showTab(index) { tab = index; refresh() }
    function openPaycheck(id) { paycheckDialog.openFor(id) }
    // The last day of the latest finished quarter: the period a filing copy covers.
    function lastQuarterEnd() {
        const d = new Date(books.today + "T00:00:00")
        const q = Math.floor(d.getMonth() / 3)
        const end = new Date(d.getFullYear(), q * 3, 0)
        return end.getFullYear() + "-" + String(end.getMonth() + 1).padStart(2, "0") + "-" + String(end.getDate()).padStart(2, "0")
    }
    function saveForFiling(id) {
        filingDialog.what = id
        filingDialog.open()
    }
    onTabChanged: refresh()
    onVisibleChanged: if (visible) { refresh(); if (filingDate.text.length === 0 && books !== null) filingDate.text = lastQuarterEnd() }
    Connections { target: view.payroll; function onChanged() { if (view.visible) view.refresh() } }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Payroll") }
            Tk.Badge { objectName: "payrollEntityBadge"; text: view.entityLabels[view.entity] || ""; variant: view.entity === "unsure" ? "warning" : "accent" }
            Tk.Spacer { }
            Tk.Button { objectName: "payrollSetup"; variant: view.configured ? "outline" : "accent"; iconName: "settings-2"; text: qsTr("Set up…")
                        onClicked: view.host.actions.trigger("booksPayrollSetupAction") }
            Tk.Button { objectName: "payrollPayOwner"; variant: "outline"; iconName: "hand-coins"
                        text: view.entity === "sCorporation" ? qsTr("Distribution…") : qsTr("Pay owner…")
                        onClicked: view.host.actions.trigger(view.entity === "sCorporation" ? "booksDistributionAction" : "booksPayOwnerAction") }
            Tk.Button { objectName: "payrollRun"; variant: view.configured ? "accent" : "outline"; iconName: "receipt"; text: qsTr("Run payroll…")
                        onClicked: view.host.actions.trigger("booksRunPayrollAction") }
        }
        // Six tabs need room; a narrow window picks the section from a list.
        Tk.TabStrip {
            objectName: "payrollTabs"
            visible: !view.narrow
            model: view.tabNames
            currentIndex: view.tab
            onTabActivated: function(index) { view.tab = index }
        }
        Tk.ComboBox {
            objectName: "payrollTabsNarrow"
            visible: view.narrow
            model: view.tabNames
            currentIndex: view.tab
            onActivated: function(index) { view.tab = index }
        }

        // ---- Overview ----
        Tk.Scroll {
            id: overview
            objectName: "payrollOverview"
            visible: view.tab === 0
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex {
                width: overview.viewportWidth
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Notice {
                    objectName: "payrollNotSetUp"
                    visible: !view.configured
                    variant: "warning"
                    title: qsTr("Set up owner pay first")
                    text: qsTr("Tell QindaBooks how the IRS taxes your LLC — Schedule C, S corporation or partnership — and it sets up the accounts, the owner's pay and, with employees, payroll.")
                    actions: Tk.Button { small: true; variant: "accent"; text: qsTr("Set up…"); onClicked: view.host.actions.trigger("booksPayrollSetupAction") }
                }
                Tk.Card {
                    objectName: "payrollHowPaid"
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Overline { title: qsTr("How you are paid") }
                        Tk.Label {
                            wrapMode: Text.Wrap
                            text: view.entity === "sCorporation"
                                  ? qsTr("You are the corporation's employee: pay yourself a reasonable salary with Run Payroll — withholding and payroll taxes included — and take the rest of the profit as shareholder distributions.")
                                  : view.entity === "scheduleC"
                                  ? qsTr("You take owner draws. The profit is taxed on your own Form 1040 (Schedule C and Schedule SE): pay federal and Colorado estimated taxes four times a year, and set part of each draw aside for them.")
                                  : view.entity === "partnership"
                                  ? qsTr("Each partner takes draws and pays federal and Colorado estimated taxes on their share of the profit (Schedule K-1).")
                                  : qsTr("Until you confirm how the IRS taxes your LLC, both ways stay open: owner draws with estimated taxes, and payroll. The CP261 notice is the sign of an S corporation election.")
                        }
                    }
                }
                Tk.SectionHeader { title: qsTr("Coming due"); count: String(view.upcoming.length) }
                Tk.DataTable {
                    objectName: "payrollUpcoming"
                    implicitHeight: Tk.Theme.size.row * (Math.max(1, Math.min(8, view.upcoming.length)) + 1.5)
                    model: view.upcoming
                    emptyText: qsTr("Nothing due in the next 45 days.")
                    columns: [
                        Tk.TableColumn { key: "date"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                        Tk.TableColumn { key: "label"; title: qsTr("What"); flex: 1 },
                        Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true }
                    ]
                }
                Tk.SectionHeader { title: qsTr("Copies for filing") }
                Tk.Caption {
                    wrapMode: Text.Wrap
                    text: qsTr("Every form is a report under Reports ▸ Payroll tax forms, with identity numbers masked. These copies show them in full, for filing — keep them private. QindaBooks does not e-file or pay: upload the W-2 file to the SSA's Business Services Online and enter the wage reports in MyUI Employer+ and My FAMLI+ Employer.")
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Caption { text: qsTr("For the period ending") }
                    Tk.TextField { id: filingDate; objectName: "payrollFilingDate"; small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth }
                    Tk.Button { objectName: "payrollFileW2"; small: true; variant: "outline"; iconName: "file-check"; text: qsTr("W-2s…"); onClicked: view.saveForFiling("form_w2") }
                    Tk.Button { objectName: "payrollFileW3"; small: true; variant: "outline"; iconName: "file-check"; text: qsTr("W-3…"); onClicked: view.saveForFiling("form_w3") }
                    Tk.Button { objectName: "payrollFileUi"; small: true; variant: "outline"; iconName: "file-check"; text: qsTr("UI wage report…"); onClicked: view.saveForFiling("ui_wage_report") }
                    Tk.Button { objectName: "payrollFileFamli"; small: true; variant: "outline"; iconName: "file-check"; text: qsTr("FAMLI wage report…"); onClicked: view.saveForFiling("famli_wage_report") }
                    Tk.Button { objectName: "payrollFileEfw2"; small: true; variant: "outline"; iconName: "file-down"; text: qsTr("W-2 file (EFW2)…"); onClicked: view.saveForFiling("efw2") }
                }
                Tk.Notice { id: filingResult; objectName: "payrollFilingResult"; visible: false; dismissible: true; onDismissed: visible = false }
            }
        }

        // ---- Owner pay ----
        Tk.Flex {
            visible: view.tab === 1
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Caption { text: qsTr("Money paid to the owners in %1: draws, distributions, estimated taxes and set-asides. None of it is an expense.").arg(view.year); wrapMode: Text.Wrap; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Button { objectName: "ownerSetAside"; small: true; variant: "outline"; iconName: "piggy-bank"; text: qsTr("Set aside…"); onClicked: setAsidePrompt.open() }
                Tk.Button { objectName: "ownerVoid"; small: true; variant: "ghost"; iconName: "circle-x"; text: qsTr("Void…"); enabled: view.ownerPick >= 0 && view.ownerRows[view.ownerPick] && !view.ownerRows[view.ownerPick].voided
                            onClicked: { voidPrompt.text = ""; voidPrompt.open() } }
            }
            Tk.DataTable {
                objectName: "ownerPayments"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                model: view.ownerRows
                alternatingRows: true
                currentIndex: view.ownerPick
                onActivated: function(index) { view.ownerPick = index }
                emptyText: qsTr("Nothing paid to an owner yet this year.")
                columns: [
                    Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                    Tk.TableColumn { key: "kindLabel"; title: qsTr("Kind"); width: Tk.Theme.size.tableColumn * 1.8 },
                    Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "ownerName"; title: qsTr("Owner"); flex: 1; minWidth: Tk.Theme.size.tableColumn * 1.4 },
                    Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn * 0.8; mono: true },
                    Tk.TableColumn { key: "memo"; title: qsTr("Memo"); flex: 1.4; minWidth: Tk.Theme.size.tableColumn * 2; muted: true },
                    Tk.TableColumn { key: "voided"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn
                                     formatter: function(value, row) { return row.voided ? qsTr("Void") : "" } }
                ]
            }
        }

        EstimatedTaxesPane {
            id: estimates
            objectName: "payrollEstimates"
            visible: view.tab === 2
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            books: view.books
            payroll: view.payroll
            onProfileRequested: profileDialog.openDialog()
            onPaymentRequested: function(jurisdiction, installment, amountPlain, year) { paymentDialog.openFor(jurisdiction, installment, amountPlain, year) }
        }
        PayrollPeoplePane {
            id: people
            visible: view.tab === 3 || view.tab === 4
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            mode: view.tab === 4 ? "paychecks" : "employees"
            host: view.host
            payroll: view.payroll
            year: view.year
            onEmployeeRequested: function(id) { employeeDialog.openFor(id) }
            onPaycheckRequested: function(id) { paycheckDialog.openFor(id) }
        }
        PayrollLiabilitiesPane {
            id: liabilities
            visible: view.tab === 5
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            host: view.host
            payroll: view.payroll
            year: view.year
        }
    }

    // ---- dialogs opened from a row ----
    EmployeePayrollDialog { id: employeeDialog; payroll: view.payroll }
    PaycheckDialog { id: paycheckDialog; host: view.host; payroll: view.payroll }
    EstimateProfileDialog { id: profileDialog; payroll: view.payroll; onAccepted: estimates.refresh() }
    EstimatedPaymentDialog { id: paymentDialog; books: view.books; payroll: view.payroll }
    Tk.PromptDialog {
        id: setAsidePrompt
        objectName: "setAsidePrompt"
        title: qsTr("Set aside for taxes")
        label: qsTr("Amount to move from the payroll bank account to the tax savings account chosen in setup.")
        primaryText: qsTr("Move")
        onAccepted: {
            const setup = view.payroll.setup()
            const r = view.payroll.recordSetAside({ amount: text, date: view.books.today, bankAccountId: setup.payrollBankAccountId,
                                                    toAccountId: setup.setAsideAccountId })
            if (!r.ok) view.books.notify(qsTr("Set aside"), String(r.error || ""), "warning")
        }
    }
    Tk.PromptDialog {
        id: voidPrompt
        objectName: "payrollVoidPrompt"
        acceptEmpty: true
        title: qsTr("Void")
        label: qsTr("Why is it void? The reason goes in the audit trail.")
        primaryText: qsTr("Void")
        onAccepted: {
            const row = view.ownerRows[view.ownerPick]
            const r = view.payroll.voidOwnerPayment(row.id, text.trim().length > 0 ? text : qsTr("Voided"))
            if (!r.ok) view.books.notify(qsTr("Void"), String(r.error || ""), "warning")
        }
    }
    Dialogs.FileDialog {
        id: filingDialog
        property string what: ""
        title: qsTr("Save for filing")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: what === "efw2" ? [qsTr("SSA W-2 file (*.txt)")] : [qsTr("PDF (*.pdf)")]
        onAccepted: {
            const r = what === "efw2" ? view.payroll.exportEfw2(Number(filingDate.text.substring(0, 4)) || view.year, selectedFile)
                                      : view.payroll.exportFormForFiling(what, filingDate.text, selectedFile)
            filingResult.variant = r.ok ? "success" : "danger"
            filingResult.title = r.ok ? qsTr("Saved") : qsTr("Not saved")
            filingResult.text = r.ok ? String(r.path || "") + ((r.warnings || []).length > 0 ? " — " + r.warnings.join(" ") : "") : String(r.error || "")
            filingResult.visible = true
        }
    }
}
