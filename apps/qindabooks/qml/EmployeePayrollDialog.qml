// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Employees ▸ an employee (M71, D-081): the payroll half of an
// M68 employee — legal name and SSN for the W-2, address, pay, the
// federal Form W-4 (2020 or later, or 2019 and earlier), Colorado's
// DR 0004, the city head tax and the deductions. Saving a new person adds
// them to the Employees list (Time uses the same list) in one step.
//
// AGENT-GUARD (identity numbers): the SSN field is the one place the
// whole number shows; everywhere else it is masked (•••-••-1234).
Tk.Dialog {
    id: dialog

    property PayrollService payroll: null
    property var record: ({})
    property var info: ({})
    property string problem: ""
    readonly property bool hourly: payTypeBox.currentIndex === 1
    readonly property bool modern: w4FormBox.currentIndex === 0
    readonly property var statuses: [{ key: "singleOrMarriedSeparately", label: qsTr("Single or married filing separately") },
                                     { key: "marriedJointly", label: qsTr("Married filing jointly") },
                                     { key: "headOfHousehold", label: qsTr("Head of household") }]
    readonly property var legacyStatuses: [{ key: "single", label: qsTr("Single") }, { key: "married", label: qsTr("Married") },
                                           { key: "marriedSingleRate", label: qsTr("Married, but withhold at the higher single rate") }]
    readonly property var retirementKinds: [{ key: "none", label: qsTr("None") }, { key: "401k", label: qsTr("401(k)") },
                                            { key: "simple", label: qsTr("SIMPLE IRA") }]
    readonly property var cities: [{ key: "", city: qsTr("The company's city") }, { key: "none", city: qsTr("None (works elsewhere)") }]
                                  .concat((info.cities || []).filter(function(c) { return String(c.key).length > 0 }))

    objectName: "employeePayrollDialog"
    title: Number(record.id || 0) > 0 ? [record.firstName, record.lastName].join(" ") : qsTr("New employee")
    subtitle: qsTr("Pay, withholding certificates and deductions")
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function keyIndex(list, key) { return Math.max(0, list.map(function(e) { return e.key }).indexOf(String(key))) }
    function openFor(id) {
        info = payroll.tablesInfo()
        record = payroll.employee(Number(id || 0))
        const r = record
        first.text = String(r.firstName || ""); middle.text = String(r.middleInitial || ""); last.text = String(r.lastName || "")
        suffix.text = String(r.suffix || ""); ssn.text = String(r.ssn || "")
        street.text = String(r.street || ""); city.text = String(r.city || ""); state.text = String(r.state || "CO"); zip.text = String(r.zip || "")
        hired.text = String(r.hireDate || ""); released.text = String(r.releaseDate || "")
        payTypeBox.currentIndex = r.payType === "hourly" ? 1 : 0
        rate.text = String(r.payRate || ""); hours.text = String(r.defaultHours || "")
        frequencyBox.currentIndex = keyIndex(r.frequencies || [], r.frequency || "biweekly")
        owner.checked = r.ownerEmployee === true; shareholder.checked = r.shareholder2Percent === true
        w4FormBox.currentIndex = Number(r.w4Form) === 2019 ? 1 : 0; w4OnFile.checked = r.w4OnFile !== false
        statusBox.currentIndex = keyIndex(statuses, r.w4Status); step2.checked = r.step2 === true
        step3.text = String(r.step3 || ""); step4a.text = String(r.step4a || ""); step4b.text = String(r.step4b || ""); step4c.text = String(r.step4c || "")
        legacyBox.currentIndex = keyIndex(legacyStatuses, r.legacyStatus); allowances.text = String(r.allowances || 0)
        legacyExtra.text = String(r.legacyExtra || ""); exempt.checked = r.exempt === true
        coloradoWithholding.checked = r.coloradoWithholding !== false; dr0004.checked = r.dr0004Filed === true
        dr0004Allowance.text = String(r.dr0004Allowance || ""); dr0004Extra.text = String(r.dr0004Extra || "")
        cityBox.currentIndex = keyIndex(cities, r.optCity || "")
        section125.text = String(r.section125 || ""); retirementBox.currentIndex = keyIndex(retirementKinds, r.retirementKind || "none")
        retirementAmount.text = String(r.retirementAmount || ""); retirementPercent.text = String(r.retirementPercent || "")
        health.text = String(r.shareholderHealth || ""); afterTax.text = String(r.afterTax || ""); afterTaxLabel.text = String(r.afterTaxLabel || "")
        problem = ""
        open()
        first.forceActiveFocus()
    }
    function collect() {
        return { id: record.id || 0, firstName: first.text, middleInitial: middle.text, lastName: last.text, suffix: suffix.text,
                 ssn: ssn.text, street: street.text, city: city.text, state: state.text, zip: zip.text,
                 hireDate: hired.text, releaseDate: released.text, payType: hourly ? "hourly" : "salary", payRate: rate.text,
                 frequency: (record.frequencies || [])[Math.max(0, frequencyBox.currentIndex)].key, defaultHours: hours.text,
                 ownerEmployee: owner.checked, shareholder2Percent: shareholder.checked,
                 w4Form: modern ? 2020 : 2019, w4OnFile: w4OnFile.checked, w4Status: statuses[statusBox.currentIndex].key,
                 step2: step2.checked, step3: step3.text, step4a: step4a.text, step4b: step4b.text, step4c: step4c.text,
                 legacyStatus: legacyStatuses[legacyBox.currentIndex].key, allowances: Number(allowances.text || 0),
                 legacyExtra: legacyExtra.text, exempt: exempt.checked, coloradoWithholding: coloradoWithholding.checked,
                 dr0004Filed: dr0004.checked, dr0004Allowance: dr0004Allowance.text, dr0004Extra: dr0004Extra.text,
                 optCity: cities[Math.max(0, cityBox.currentIndex)].key, section125: section125.text,
                 retirementKind: retirementKinds[retirementBox.currentIndex].key, retirementAmount: retirementAmount.text,
                 retirementPercent: retirementPercent.text, shareholderHealth: health.text, afterTax: afterTax.text,
                 afterTaxLabel: afterTaxLabel.text }
    }
    // The save step; the tests fill the fields and call it directly.
    function save() {
        const r = payroll.saveEmployee(collect())
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) record = payroll.employee(r.id)
        return r
    }
    onAccepted: { if (!save().ok) open() }

    component Field: Tk.TextField { small: true }
    component Amount: Tk.TextField { small: true; mono: true; horizontalAlignment: Text.AlignRight }
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    component Pair: Tk.Grid { columnGap: Tk.Theme.space.lg; rowGap: Tk.Theme.space.sm }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "employeePayrollProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: dialog.problem }
        Tk.SectionHeader { title: qsTr("Employee (as on the Social Security card)") }
        Pair {
            columns: dialog.fieldColumns
            Tk.PropertyRow { label: qsTr("First name"); Field { id: first; objectName: "payrollFirstName" } }
            Tk.PropertyRow { label: qsTr("Middle initial"); Field { id: middle; objectName: "payrollMiddle" } }
            Tk.PropertyRow { label: qsTr("Last name"); Field { id: last; objectName: "payrollLastName" } }
            Tk.PropertyRow { label: qsTr("Suffix"); Field { id: suffix; objectName: "payrollSuffix" } }
            Tk.PropertyRow { label: qsTr("SSN"); hint: qsTr("Shown in full only here; masked everywhere else."); Field { id: ssn; objectName: "payrollSsn"; mono: true; placeholderText: "123-45-6789" } }
            Tk.PropertyRow { label: qsTr("Street"); Field { id: street; objectName: "payrollStreet" } }
            Tk.PropertyRow { label: qsTr("City"); Field { id: city; objectName: "payrollCityName" } }
            Tk.PropertyRow {
                label: qsTr("State, ZIP")
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Field { id: state; objectName: "payrollState"; Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.6; Tk.Flex.shrink: 0 }
                    Field { id: zip; objectName: "payrollZip"; mono: true; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                }
            }
            Tk.PropertyRow { label: qsTr("Hired"); Field { id: hired; objectName: "payrollHired"; mono: true; placeholderText: "yyyy-mm-dd" } }
            Tk.PropertyRow { label: qsTr("Released"); Field { id: released; objectName: "payrollReleased"; mono: true; placeholderText: qsTr("Still employed") } }
        }
        Tk.SectionHeader { title: qsTr("Pay") }
        Pair {
            columns: dialog.fieldColumns
            Tk.PropertyRow { label: qsTr("Paid"); Tk.Segmented { id: payTypeBox; objectName: "payrollPayType"; small: true; model: [qsTr("Salary"), qsTr("Hourly")] } }
            Tk.PropertyRow {
                label: dialog.hourly ? qsTr("Hourly rate") : qsTr("Salary a year")
                Amount { id: rate; objectName: "payrollRate" }
            }
            Tk.PropertyRow {
                label: qsTr("Pay schedule")
                Tk.ComboBox { id: frequencyBox; objectName: "payrollFrequency"; small: true; textRole: "label"; model: dialog.record.frequencies || [] }
            }
            Tk.PropertyRow { visible: dialog.hourly; label: qsTr("Usual hours"); hint: qsTr("Each pay period; Run Payroll starts from them."); Amount { id: hours; objectName: "payrollHours" } }
            Tk.PropertyRow {
                label: qsTr("Owner (officer)"); hint: qsTr("An S corporation owner on salary: wages post to Officer Wages.")
                Tk.Switch { id: owner; objectName: "payrollOwnerEmployee"; small: true }
            }
            Tk.PropertyRow {
                label: qsTr("2% shareholder"); hint: qsTr("Owns more than 2% of an S corporation: health insurance the company pays is wages (W-2 box 1), not FICA.")
                Tk.Switch { id: shareholder; objectName: "payrollShareholder"; small: true }
            }
        }
        Tk.SectionHeader { title: qsTr("Federal Form W-4") }
        Pair {
            columns: dialog.fieldColumns
            Tk.PropertyRow {
                label: qsTr("Form")
                Tk.Segmented { id: w4FormBox; objectName: "payrollW4Form"; small: true; model: [qsTr("2020 or later"), qsTr("2019 or earlier")] }
            }
            Tk.PropertyRow {
                label: qsTr("On file"); hint: qsTr("Without one, Publication 15-T withholds as single with no adjustments.")
                Tk.Switch { id: w4OnFile; objectName: "payrollW4OnFile"; small: true }
            }
            Tk.PropertyRow {
                visible: dialog.modern; label: qsTr("Step 1(c)")
                Tk.ComboBox { id: statusBox; objectName: "payrollW4Status"; small: true; textRole: "label"; model: dialog.statuses }
            }
            Tk.PropertyRow { visible: dialog.modern; label: qsTr("Step 2 checked"); Tk.Switch { id: step2; objectName: "payrollStep2"; small: true } }
            Tk.PropertyRow { visible: dialog.modern; label: qsTr("Step 3"); hint: qsTr("Credits, a year."); Amount { id: step3; objectName: "payrollStep3" } }
            Tk.PropertyRow { visible: dialog.modern; label: qsTr("Step 4(a)"); hint: qsTr("Other income, a year."); Amount { id: step4a; objectName: "payrollStep4a" } }
            Tk.PropertyRow { visible: dialog.modern; label: qsTr("Step 4(b)"); hint: qsTr("Deductions, a year."); Amount { id: step4b; objectName: "payrollStep4b" } }
            Tk.PropertyRow { visible: dialog.modern; label: qsTr("Step 4(c)"); hint: qsTr("Extra withholding each paycheck."); Amount { id: step4c; objectName: "payrollStep4c" } }
            Tk.PropertyRow {
                visible: !dialog.modern; label: qsTr("Marital status")
                Tk.ComboBox { id: legacyBox; objectName: "payrollLegacyStatus"; small: true; textRole: "label"; model: dialog.legacyStatuses }
            }
            Tk.PropertyRow { visible: !dialog.modern; label: qsTr("Allowances"); Amount { id: allowances; objectName: "payrollAllowances" } }
            Tk.PropertyRow { visible: !dialog.modern; label: qsTr("Extra"); hint: qsTr("Additional withholding each paycheck."); Amount { id: legacyExtra; objectName: "payrollLegacyExtra" } }
            Tk.PropertyRow { label: qsTr("Exempt"); hint: qsTr("Claimed exemption: no federal income tax withheld."); Tk.Switch { id: exempt; objectName: "payrollExempt"; small: true } }
        }
        Tk.SectionHeader { title: qsTr("Colorado DR 0004 and city tax") }
        Pair {
            columns: dialog.fieldColumns
            Tk.PropertyRow {
                label: qsTr("Colorado tax"); hint: qsTr("Withhold Colorado income tax; off for a nonresident working outside Colorado.")
                Tk.Switch { id: coloradoWithholding; objectName: "payrollColorado"; small: true }
            }
            Tk.PropertyRow {
                label: qsTr("DR 0004 on file"); hint: qsTr("Without one, Colorado withholds from the W-4's filing status.")
                Tk.Switch { id: dr0004; objectName: "payrollDr0004"; small: true }
            }
            Tk.PropertyRow { visible: dr0004.checked; label: qsTr("Line 2"); hint: qsTr("Annual withholding allowance; blank if left blank."); Amount { id: dr0004Allowance; objectName: "payrollDr0004Allowance"; placeholderText: qsTr("Blank") } }
            Tk.PropertyRow { visible: dr0004.checked; label: qsTr("Line 3"); hint: qsTr("Extra withholding each paycheck."); Amount { id: dr0004Extra; objectName: "payrollDr0004Extra" } }
            Tk.PropertyRow {
                label: qsTr("City tax (OPT)")
                Tk.ComboBox { id: cityBox; objectName: "payrollOptCity"; small: true; textRole: "city"; model: dialog.cities }
            }
        }
        Tk.SectionHeader { title: qsTr("Deductions each paycheck") }
        Pair {
            columns: dialog.fieldColumns
            Tk.PropertyRow {
                label: qsTr("Health (pre-tax)"); hint: qsTr("Each paycheck, through a Section 125 plan: before income tax, social security and Medicare.")
                Amount { id: section125; objectName: "payrollSection125" }
            }
            Tk.PropertyRow {
                label: qsTr("Retirement plan")
                Tk.ComboBox { id: retirementBox; objectName: "payrollRetirement"; small: true; textRole: "label"; model: dialog.retirementKinds }
            }
            Tk.PropertyRow { visible: retirementBox.currentIndex > 0; label: qsTr("Amount"); hint: qsTr("Each paycheck."); Amount { id: retirementAmount; objectName: "payrollRetirementAmount" } }
            Tk.PropertyRow {
                visible: retirementBox.currentIndex > 0; label: qsTr("Or percent")
                Amount { id: retirementPercent; objectName: "payrollRetirementPercent"; placeholderText: "5%" }
            }
            Tk.PropertyRow {
                visible: shareholder.checked; label: qsTr("Health premium"); hint: qsTr("Paid by the company each paycheck for the shareholder; added to W-2 box 1.")
                Amount { id: health; objectName: "payrollShareholderHealth" }
            }
            Tk.PropertyRow { label: qsTr("After tax"); hint: qsTr("Deducted each paycheck after taxes."); Amount { id: afterTax; objectName: "payrollAfterTax" } }
            Tk.PropertyRow { label: qsTr("For"); Field { id: afterTaxLabel; objectName: "payrollAfterTaxLabel"; placeholderText: qsTr("Garnishment, loan repayment…") } }
        }
    }
}
