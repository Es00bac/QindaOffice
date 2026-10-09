// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Set Up Owner Pay & Payroll… (M71, D-081): how the business is
// taxed — which decides how its owners are paid — its tax account numbers,
// where it pays payroll taxes, and the employer's own payroll values.
// Federal taxes are built in; Colorado is the first state with built-in
// tables (its section shows only when chosen); any other state's or city's
// payroll taxes are payroll tax items the business defines. "Not sure"
// explains how to tell and keeps every way of paying owners open. The one
// statement about the tables and review lives here (and in
// docs/payroll-tax-tables.md), nowhere else.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property PayrollService payroll: null
    property var record: ({})
    property var info: ({})
    property var accounts: []
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var asideAccounts: [{ id: 0, name: qsTr("None (no transfer)") }].concat(accounts)
    property string entity: "unsure"
    property string problem: ""
    readonly property bool draws: entity === "scheduleC" || entity === "partnership" || entity === "unsure"
    // A business outside Colorado sees none of Colorado's open questions.
    readonly property var unconfirmed: (info.unconfirmed || []).filter(function(u) {
        return coloradoOn.checked || String(u.path).indexOf("colorado.") !== 0 })
    readonly property var entities: [
        { text: qsTr("Sole proprietorship or single-member LLC (Schedule C)"), value: "scheduleC" },
        { text: qsTr("Partnership or multi-member LLC"), value: "partnership" },
        { text: qsTr("S corporation"), value: "sCorporation" },
        { text: qsTr("C corporation"), value: "cCorporation" },
        { text: qsTr("Nonprofit"), value: "nonprofit" },
        { text: qsTr("Not sure"), value: "unsure" }]
    readonly property var help: ({
        scheduleC: qsTr("A sole proprietorship, or a single-member LLC that never elected otherwise, reports its profit on the owner's Schedule C. The owner is not the business's employee: the business pays its owner with owner's draws, and the owner pays estimated tax on the profit — the planner under Payroll ▸ Estimated taxes helps."),
        partnership: qsTr("A partnership, or an LLC with two or more members and no election, files Form 1065. Partners are not employees: each takes owner's draws and pays estimated tax on their share of the profit."),
        sCorporation: qsTr("The IRS accepted the business's S corporation election (Form 2553) — the CP261 notice says so. An owner who works in it is paid a reasonable salary through payroll, with withholding and payroll taxes, and profit beyond it as shareholder distributions. What is reasonable is the business's and its accountant's call; QindaBooks records it."),
        cCorporation: qsTr("A corporation that files Form 1120. Owners who work in it are paid a salary through payroll; profit paid to its shareholders is a dividend, recorded against retained earnings. It takes no owner's draws."),
        nonprofit: qsTr("A tax-exempt organization that files Form 990. It has no owners to pay: everyone who works for it is paid through payroll."),
        unsure: qsTr("How to tell: an IRS CP261 notice means an S corporation election (Form 2553) was accepted. Without an election, a single-member LLC reports on Schedule C and a multi-member LLC files as a partnership; a corporation files Form 1120, a nonprofit Form 990. Until the type is set, QindaBooks keeps owner's draws, distributions, dividends and payroll open.") })

    objectName: "payrollSetupDialog"
    title: qsTr("Owner pay and payroll")
    subtitle: qsTr("How the business is taxed decides how its owners are paid")
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function indexIn(list, id) { for (let i = 0; i < list.length; ++i) if (Number(list[i].id) === Number(id)) return i; return 0 }
    function openDialog() {
        record = payroll.setup()
        info = payroll.tablesInfo()
        accounts = books.bankAccounts()
        entity = String(record.entity || "unsure")
        einField.text = String(record.ein || ""); canField.text = String(record.coloradoAccount || "")
        uiAccountField.text = String(record.uiAccount || ""); famliAccountField.text = String(record.famliAccount || "")
        coloradoOn.checked = record.colorado === true; stateField.text = String(record.stateCode || "")
        depositBox.currentIndex = record.depositSchedule === "semiweekly" ? 1 : 0
        frequencyBox.currentIndex = Math.max(0, ["quarterly", "monthly", "weekly"].indexOf(String(record.coloradoFrequency || "quarterly")))
        uiRateField.text = String(record.uiRate || ""); heavy.checked = record.uiHeavyConstruction === true
        small.checked = record.smallEmployer !== false
        employeeShare.text = String(record.famliEmployeeShare || "50%"); employerShare.text = String(record.famliEmployerShare || "0%")
        const cities = info.cities || []
        cityBox.currentIndex = Math.max(0, cities.map(function(c) { return c.key }).indexOf(String(record.optCity || "")))
        optFilingBox.currentIndex = record.optFiling === "monthly" ? 1 : 0
        roundFederal.checked = record.roundFederalWithholding === true
        bankBox.currentIndex = indexIn(accounts, record.payrollBankAccountId); asideBox.currentIndex = indexIn(asideAccounts, record.setAsideAccountId)
        bsoField.text = String(record.bsoUserId || ""); contactField.text = String(record.w2ContactName || "")
        phoneField.text = String(record.w2ContactPhone || ""); emailField.text = String(record.w2ContactEmail || "")
        pinField.text = String(record.signaturePin || "")
        problem = ""
        open()
    }
    function collect() {
        const cities = info.cities || []
        return { entity: entity, ein: einField.text, colorado: coloradoOn.checked, stateCode: coloradoOn.checked ? "" : stateField.text,
                 coloradoAccount: canField.text, uiAccount: uiAccountField.text,
                 famliAccount: famliAccountField.text, depositSchedule: depositBox.currentIndex === 1 ? "semiweekly" : "monthly",
                 coloradoFrequency: ["quarterly", "monthly", "weekly"][Math.max(0, frequencyBox.currentIndex)],
                 uiRate: uiRateField.text, uiHeavyConstruction: heavy.checked, smallEmployer: small.checked,
                 famliEmployeeShare: employeeShare.text, famliEmployerShare: employerShare.text,
                 optCity: cities.length > 0 ? cities[Math.max(0, cityBox.currentIndex)].key : "",
                 optFiling: optFilingBox.currentIndex === 1 ? "monthly" : "quarterly", roundFederalWithholding: roundFederal.checked,
                 payrollBankAccountId: accounts.length > 0 ? accounts[bankBox.currentIndex].id : 0,
                 setAsideAccountId: asideAccounts[Math.max(0, asideBox.currentIndex)].id,
                 bsoUserId: bsoField.text, w2ContactName: contactField.text, w2ContactPhone: phoneField.text,
                 w2ContactEmail: emailField.text, signaturePin: pinField.text }
    }
    // The save step; the tests fill the fields and call it directly.
    function save() {
        const r = payroll.saveSetup(collect())
        problem = r.ok ? "" : String(r.error || "")
        return r
    }
    onAccepted: { if (!save().ok) open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "payrollSetupProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: dialog.problem }
        Tk.Notice {
            objectName: "payrollTablesNotice"
            variant: dialog.info.ok === false ? "danger" : "info"
            title: dialog.info.ok === false ? qsTr("No tax tables for this year") : qsTr("Computed from the official %1 tables").arg(dialog.info.year || "")
            text: dialog.info.ok === false ? String(dialog.info.error || "")
                : qsTr("QindaBooks computes paychecks and estimated taxes from the official %1 federal tables — and Colorado's, the first state with built-in tables — listed under Sources with their dates. Another state's or city's payroll taxes are payroll tax items the business defines. Confirm how the IRS taxes the business, and have a tax professional review its first payroll and its first estimate. QindaBooks prepares the numbers and the forms; filing and paying happen through the agencies' own systems (EFTPS for federal deposits).").arg(dialog.info.year || "")
        }
        Tk.SectionHeader { title: qsTr("How is the business taxed?") }
        Tk.ComboBox {
            objectName: "payrollEntity"
            textRole: "text"
            model: dialog.entities
            currentIndex: Math.max(0, dialog.entities.map(function(e) { return e.value }).indexOf(dialog.entity))
            onActivated: function(index) { dialog.entity = dialog.entities[index].value }
        }
        Tk.Caption { objectName: "payrollEntityHelp"; wrapMode: Text.Wrap; text: String(dialog.help[dialog.entity] || "") }
        Tk.SectionHeader { title: qsTr("Where the business pays payroll taxes") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("EIN"); Tk.TextField { id: einField; objectName: "payrollEin"; small: true; mono: true; placeholderText: "12-3456789" } }
            Tk.PropertyRow {
                label: qsTr("Colorado"); hint: qsTr("The first state with built-in tables: withholding, UI, FAMLI, city taxes and DR 0104EP estimates.")
                Tk.Switch { id: coloradoOn; objectName: "payrollColoradoOn"; small: true }
            }
            Tk.PropertyRow {
                visible: !coloradoOn.checked; label: qsTr("State"); hint: qsTr("Where state unemployment tax is paid (Form 940 line 1a), as its two letters.")
                Tk.TextField { id: stateField; objectName: "payrollStateCode"; small: true; mono: true; placeholderText: "TX" }
            }
            Tk.PropertyRow {
                label: qsTr("Other taxes"); hint: qsTr("Any other state's or city's payroll taxes and state withholding: a name, a rate, a wage base, the agency.")
                Tk.Button { objectName: "payrollItemsButton"; small: true; variant: "outline"; iconName: "list-plus"; text: qsTr("Payroll tax items…")
                            onClicked: if (dialog.host !== null) dialog.host.actions.trigger("booksPayrollItemsAction") }
            }
        }
        Tk.SectionHeader { title: qsTr("Payroll") }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("For a business with employees, a corporation's officers included. A sole proprietor or a partner is never the business's own employee.")
        }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Federal deposits"); hint: qsTr("New employers deposit monthly; the IRS says when a business becomes semiweekly.")
                Tk.ComboBox { id: depositBox; objectName: "payrollDeposit"; small: true; model: [qsTr("Monthly"), qsTr("Semiweekly")] }
            }
            Tk.PropertyRow {
                label: qsTr("Whole dollars"); hint: qsTr("Round federal withholding to the dollar; Publication 15-T allows it when always done.")
                Tk.Switch { id: roundFederal; objectName: "payrollRoundFederal"; small: true }
            }
            Tk.PropertyRow { label: qsTr("Payroll bank"); Tk.ComboBox { id: bankBox; objectName: "payrollBank"; small: true; textRole: "name"; model: dialog.accounts } }
        }
        Tk.SectionHeader { visible: coloradoOn.checked; title: qsTr("Colorado") }
        Tk.Grid {
            visible: coloradoOn.checked
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Colorado CAN"); hint: qsTr("Colorado withholding account number."); Tk.TextField { id: canField; objectName: "payrollCan"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("UI account"); Tk.TextField { id: uiAccountField; objectName: "payrollUiAccount"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("FAMLI account"); Tk.TextField { id: famliAccountField; objectName: "payrollFamliAccount"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("Colorado returns")
                hint: qsTr("Quarterly under %1 of withholding a year, monthly to %2, weekly above.").arg(dialog.info.coloradoMonthlyFrom || "").arg(dialog.info.coloradoWeeklyAbove || "")
                Tk.ComboBox { id: frequencyBox; objectName: "payrollCoFrequency"; small: true; model: [qsTr("Quarterly"), qsTr("Monthly"), qsTr("Weekly")] }
            }
            Tk.PropertyRow {
                label: qsTr("UI rate")
                hint: qsTr("The rate CDLE assigned, its parts as on the notice, e.g. \"1.53%, 0.17%, 1.35%\". Empty: the %1 new-employer rate.").arg(dialog.info.introductoryRateText || "")
                Tk.TextField { id: uiRateField; objectName: "payrollUiRate"; small: true; mono: true; placeholderText: String(dialog.info.introductoryRate || "") }
            }
            Tk.PropertyRow { label: qsTr("Heavy construction"); Tk.Switch { id: heavy; objectName: "payrollHeavy"; small: true } }
            Tk.PropertyRow {
                label: qsTr("Small employer")
                hint: qsTr("Fewer than %1 employees: FAMLI does not require the employer half.").arg(dialog.info.famliSmallEmployerSize || "")
                Tk.Switch { id: small; objectName: "payrollSmallEmployer"; small: true; onToggled: employerShare.text = checked ? "0%" : "50%" }
            }
            Tk.PropertyRow { label: qsTr("Employee FAMLI"); hint: qsTr("The share of the premium withheld from pay."); Tk.TextField { id: employeeShare; objectName: "payrollFamliEmployee"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("Company FAMLI"); hint: qsTr("The share the company pays."); Tk.TextField { id: employerShare; objectName: "payrollFamliEmployer"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("City tax (OPT)")
                Tk.ComboBox { id: cityBox; objectName: "payrollCity"; small: true; textRole: "city"; model: dialog.info.cities || [] }
            }
            Tk.PropertyRow { label: qsTr("OPT returns"); Tk.ComboBox { id: optFilingBox; objectName: "payrollOptFiling"; small: true; model: [qsTr("Quarterly"), qsTr("Monthly")] } }
        }
        Tk.SectionHeader { visible: dialog.draws; title: qsTr("Owner's draws") }
        Tk.PropertyRow {
            visible: dialog.draws
            label: qsTr("Tax savings"); hint: qsTr("Where the share of each draw set aside for taxes goes (a savings account).")
            Tk.ComboBox { id: asideBox; objectName: "payrollAside"; small: true; textRole: "name"; model: dialog.asideAccounts }
        }
        Tk.SectionHeader { title: qsTr("W-2 file (SSA Business Services Online)") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("BSO user ID"); Tk.TextField { id: bsoField; objectName: "payrollBso"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("Contact"); Tk.TextField { id: contactField; objectName: "payrollContact"; small: true } }
            Tk.PropertyRow { label: qsTr("Phone"); Tk.TextField { id: phoneField; objectName: "payrollPhone"; small: true } }
            Tk.PropertyRow { label: qsTr("E-mail"); Tk.TextField { id: emailField; objectName: "payrollEmail"; small: true } }
            Tk.PropertyRow { label: qsTr("Signature PIN"); hint: qsTr("Optional: S-XXXXXX-Y from SSA."); Tk.TextField { id: pinField; objectName: "payrollPin"; small: true; mono: true } }
        }
        Tk.SectionHeader { id: sourcesHeader; objectName: "payrollSources"; title: qsTr("Sources"); count: String((dialog.info.sources || []).length); collapsible: true; collapsed: true }
        Repeater {
            model: sourcesHeader.collapsed ? [] : (dialog.info.sources || [])
            delegate: Tk.Caption {
                required property var modelData
                wrapMode: Text.Wrap
                text: String(modelData.title) + " — " + String(modelData.revised)
            }
        }
        Tk.Notice {
            objectName: "payrollUnconfirmed"
            visible: dialog.unconfirmed.length > 0
            variant: "warning"
            title: qsTr("Values needing confirmation")
            text: dialog.unconfirmed.map(function(u) { return u.label + ": " + u.note }).join("\n")
        }
    }
}
