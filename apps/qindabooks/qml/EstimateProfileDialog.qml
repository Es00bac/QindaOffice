// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Estimated taxes ▸ the owner's tax picture (M71, D-081): what the books
// cannot know about the return of the sole proprietor or partner the
// planner is for — filing status, their share of the profit, income and
// withholding from elsewhere, deductions, credits and last year's tax for
// the safe harbor (Colorado's fields only for a business that chose
// Colorado). Saved with the company (PayrollService::
// saveEstimateProfile); every amount is typed text the service validates.
// Empty means none, except "This year's profit": empty projects the
// books' profit so far over the whole year.
Tk.Dialog {
    id: dialog

    property PayrollService payroll: null
    property string problem: ""
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var statuses: [{ key: "single", label: qsTr("Single") },
                                     { key: "marriedJointly", label: qsTr("Married filing jointly") },
                                     { key: "marriedSeparately", label: qsTr("Married filing separately") },
                                     { key: "headOfHousehold", label: qsTr("Head of household") },
                                     { key: "qualifyingSurvivingSpouse", label: qsTr("Qualifying surviving spouse") }]
    readonly property var moneyKeys: ["projectedProfit", "otherIncome", "wagesForSocialSecurity", "wagesForMedicare",
                                      "businessAdjustments", "otherAdjustments", "itemizedDeductions", "additionalDeductions",
                                      "credits", "federalWithholding", "coloradoWithholding", "priorYearTax", "priorYearAgi",
                                      "priorYearColoradoTax"]

    objectName: "estimateProfileDialog"
    title: qsTr("Owner's tax picture")
    subtitle: qsTr("What the books cannot know about the owner's return, for the estimated-tax planner")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.8
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function field(key) {
        return { projectedProfit: projected, otherIncome: other, wagesForSocialSecurity: ssWages, wagesForMedicare: medicareWages,
                 businessAdjustments: businessAdj, otherAdjustments: otherAdj, itemizedDeductions: itemized,
                 additionalDeductions: additional, credits: credits, federalWithholding: federalHeld,
                 coloradoWithholding: coloradoHeld, priorYearTax: priorTax, priorYearAgi: priorAgi,
                 priorYearColoradoTax: priorColorado }[key]
    }
    function openDialog() {
        const p = payroll.estimateProfile()
        statusBox.currentIndex = Math.max(0, statuses.map(function(s) { return s.key }).indexOf(String(p.filingStatus || "single")))
        share.text = String(p.profitShare || "100%")
        for (const key of moneyKeys) field(key).text = String(p[key] || "")
        fullYear.checked = p.priorYearFull !== false
        problem = ""
        open()
    }
    function collect() {
        const out = { filingStatus: statuses[Math.max(0, statusBox.currentIndex)].key, profitShare: share.text, priorYearFull: fullYear.checked }
        for (const key of moneyKeys) out[key] = field(key).text.trim()
        return out
    }
    // The save step; the tests fill the fields and call it directly.
    function save() {
        const r = payroll.saveEstimateProfile(collect())
        problem = r.ok ? "" : String(r.error || "")
        return r
    }
    onAccepted: { if (!save().ok) open() }

    component Amount: Tk.TextField { small: true; mono: true; horizontalAlignment: Text.AlignRight; placeholderText: "0.00" }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "estimateProfileProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: dialog.problem }
        Tk.SectionHeader { title: qsTr("This year") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Filing status")
                Tk.ComboBox { id: statusBox; objectName: "estimateStatus"; small: true; textRole: "label"; model: dialog.statuses }
            }
            Tk.PropertyRow {
                label: qsTr("Profit share"); hint: qsTr("The owner's share of the profit: 100% for a sole proprietor, the partnership share for a partner.")
                Tk.TextField { id: share; objectName: "estimateShare"; small: true; mono: true }
            }
            Tk.PropertyRow {
                label: qsTr("This year's profit"); hint: qsTr("Empty: the books' profit so far, projected over the year.")
                Amount { id: projected; objectName: "estimateProjected"; placeholderText: qsTr("From the books") }
            }
            Tk.PropertyRow {
                label: qsTr("Other income"); hint: qsTr("Interest, dividends, a spouse's income on a joint return…")
                Amount { id: other; objectName: "estimateOtherIncome" }
            }
            Tk.PropertyRow {
                label: qsTr("W-2 box 3 wages"); hint: qsTr("Social security wages from a job elsewhere.")
                Amount { id: ssWages; objectName: "estimateSsWages" }
            }
            Tk.PropertyRow { label: qsTr("W-2 box 5 wages"); hint: qsTr("Medicare wages from a job elsewhere."); Amount { id: medicareWages; objectName: "estimateMedicareWages" } }
            Tk.PropertyRow {
                label: qsTr("SE adjustments"); hint: qsTr("Self-employed health insurance, SEP or SIMPLE contributions.")
                Amount { id: businessAdj; objectName: "estimateBusinessAdjustments" }
            }
            Tk.PropertyRow { label: qsTr("Other adjustments"); hint: qsTr("Schedule 1, Part II."); Amount { id: otherAdj; objectName: "estimateOtherAdjustments" } }
            Tk.PropertyRow {
                label: qsTr("Itemized deductions"); hint: qsTr("Empty: the standard deduction.")
                Amount { id: itemized; objectName: "estimateItemized" }
            }
            Tk.PropertyRow {
                label: qsTr("Extra deductions"); hint: qsTr("Form 1040-ES worksheet line 2b, such as the amount for age 65.")
                Amount { id: additional; objectName: "estimateAdditional" }
            }
            Tk.PropertyRow { label: qsTr("Credits"); hint: qsTr("Child tax credit and other credits."); Amount { id: credits; objectName: "estimateCredits" } }
            Tk.PropertyRow { label: qsTr("Federal tax withheld"); hint: qsTr("Expected for the year, from wages elsewhere."); Amount { id: federalHeld; objectName: "estimateFederalHeld" } }
            Tk.PropertyRow { visible: dialog.payroll !== null && dialog.payroll.colorado; label: qsTr("Colorado withheld"); hint: qsTr("Expected for the year, from wages elsewhere."); Amount { id: coloradoHeld; objectName: "estimateColoradoHeld" } }
        }
        Tk.SectionHeader { title: qsTr("Last year (the safe harbor)") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Prior federal tax"); hint: qsTr("Last year's Form 1040 line 24; empty if the owner filed none.")
                Amount { id: priorTax; objectName: "estimatePriorTax"; placeholderText: "" }
            }
            Tk.PropertyRow {
                label: qsTr("Prior AGI"); hint: qsTr("Last year's Form 1040 line 11: a high income raises the safe harbor to 110%.")
                Amount { id: priorAgi; objectName: "estimatePriorAgi"; placeholderText: "" }
            }
            Tk.PropertyRow {
                visible: dialog.payroll !== null && dialog.payroll.colorado
                label: qsTr("Prior Colorado tax"); hint: qsTr("Last year's DR 0104 net tax.")
                Amount { id: priorColorado; objectName: "estimatePriorColorado"; placeholderText: "" }
            }
            Tk.PropertyRow {
                label: qsTr("Full-year return"); hint: qsTr("Last year's return covered twelve months.")
                Tk.Switch { id: fullYear; objectName: "estimatePriorFullYear"; small: true }
            }
        }
    }
}
