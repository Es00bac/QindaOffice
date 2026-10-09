// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Set Up Owner Pay & Payroll… (M71, D-081): how the IRS taxes
// the LLC — which decides how the owner is paid — the company's tax
// account numbers, and the employer's own payroll values (deposit
// schedule, Colorado filing frequency, the UI rate CDLE assigned, the
// FAMLI shares, the city's head tax). "Not sure" explains how to tell and
// keeps both ways open. The one statement about the tables and review
// lives here (and in docs/payroll-tax-tables.md), nowhere else.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property PayrollService payroll: null
    property var record: ({})
    property var info: ({})
    property var accounts: []
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var asideAccounts: [{ id: 0, name: qsTr("None (no transfer)") }].concat(accounts)
    property string entity: "unsure"
    property string problem: ""
    readonly property var entities: [{ text: qsTr("Schedule C"), value: "scheduleC" }, { text: qsTr("S corporation"), value: "sCorporation" },
                                     { text: qsTr("Partnership"), value: "partnership" }, { text: qsTr("Not sure"), value: "unsure" }]

    objectName: "payrollSetupDialog"
    title: qsTr("Owner pay and payroll")
    subtitle: qsTr("How your LLC is taxed decides how you pay yourself")
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
        return { entity: entity, ein: einField.text, coloradoAccount: canField.text, uiAccount: uiAccountField.text,
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
                : qsTr("QindaBooks computes paychecks and estimated taxes from the official %1 IRS and Colorado tables listed under Sources, with their dates. Confirm how the IRS taxes your LLC, and have a tax professional review your first payroll or estimate. QindaBooks prepares the numbers and the forms; you file and pay through EFTPS, Revenue Online, MyUI Employer+ and My FAMLI+ Employer.").arg(dialog.info.year || "")
        }
        Tk.SectionHeader { title: qsTr("How does the IRS tax your LLC?") }
        Tk.Segmented {
            objectName: "payrollEntity"
            model: dialog.entities
            currentIndex: dialog.entities.map(function(e) { return e.value }).indexOf(dialog.entity)
            onActivated: function(index) { dialog.entity = dialog.entities[index].value }
        }
        Tk.Caption {
            objectName: "payrollEntityHelp"
            wrapMode: Text.Wrap
            text: dialog.entity === "scheduleC"
                  ? qsTr("A single-member LLC that never elected otherwise is \"disregarded\": its profit goes on Schedule C of your Form 1040. You are not an employee — you pay yourself with owner draws, and you pay federal and Colorado estimated taxes four times a year on the profit (income tax and self-employment tax).")
                  : dialog.entity === "sCorporation"
                  ? qsTr("You filed Form 2553 and the IRS accepted it (the CP261 letter says so). You must pay yourself a reasonable salary through payroll — with withholding, social security, Medicare and unemployment taxes — before taking the rest as shareholder distributions. What is reasonable is your and your accountant's call; QindaBooks records the salary and the distributions.")
                  : dialog.entity === "partnership"
                  ? qsTr("An LLC with two or more members and no election files Form 1065. Partners are not employees: each takes draws and pays estimated taxes on their share of the profit.")
                  : qsTr("How to tell: look for the IRS CP261 notice — it accepts an S corporation election (Form 2553). Without one, a single-member LLC reports on Schedule C and a multi-member LLC files as a partnership. Until you know, QindaBooks keeps both ways open: owner draws with estimated taxes, and payroll.")
        }
        Tk.SectionHeader { title: qsTr("Account numbers") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("EIN"); Tk.TextField { id: einField; objectName: "payrollEin"; small: true; mono: true; placeholderText: "12-3456789" } }
            Tk.PropertyRow { label: qsTr("Colorado CAN"); hint: qsTr("Colorado withholding account number."); Tk.TextField { id: canField; objectName: "payrollCan"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("UI account"); Tk.TextField { id: uiAccountField; objectName: "payrollUiAccount"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("FAMLI account"); Tk.TextField { id: famliAccountField; objectName: "payrollFamliAccount"; small: true; mono: true } }
        }
        Tk.SectionHeader { title: qsTr("Payroll") }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("Only for a business with employees — or an S corporation paying its owner a salary. Any LLC with employees runs payroll; the owner of a Schedule C or partnership LLC is never one of them.")
        }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Federal deposits"); hint: qsTr("New employers deposit monthly; the IRS tells you if you become semiweekly.")
                Tk.ComboBox { id: depositBox; objectName: "payrollDeposit"; small: true; model: [qsTr("Monthly"), qsTr("Semiweekly")] }
            }
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
            Tk.PropertyRow {
                label: qsTr("Whole dollars"); hint: qsTr("Round federal withholding to the dollar; Publication 15-T allows it when you always do it.")
                Tk.Switch { id: roundFederal; objectName: "payrollRoundFederal"; small: true }
            }
            Tk.PropertyRow { label: qsTr("Payroll bank"); Tk.ComboBox { id: bankBox; objectName: "payrollBank"; small: true; textRole: "name"; model: dialog.accounts } }
        }
        Tk.SectionHeader { title: qsTr("Owner pay") }
        Tk.PropertyRow {
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
            visible: (dialog.info.unconfirmed || []).length > 0
            variant: "warning"
            title: qsTr("Values needing confirmation")
            text: (dialog.info.unconfirmed || []).map(function(u) { return u.label + ": " + u.note }).join("\n")
        }
    }
}
