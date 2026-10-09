// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// One paycheck (M71, D-081): the pay stub as it prints on the voucher —
// each earning, deduction and tax for this check and the year to date —
// the employer's taxes, and the worksheets it was computed with (Pub 15-T
// Worksheet 1A, DR 1098) line by line. A paycheck is never edited in
// place: Adjust voids it and writes the corrected one (the year to date
// and every later form follow), Void voids it with a reason. Print sends
// it to the check printer with its stub (M63/M68 check path).
Tk.Dialog {
    id: dialog

    property BooksHost host: null
    property PayrollService payroll: null
    property var check: ({})
    property string mode: "view"            // view | adjust | void
    property string problem: ""
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"

    objectName: "paycheckDialog"
    title: String(check.employeeName || qsTr("Paycheck"))
    subtitle: qsTr("Paid %1 for %2 to %3%4").arg(check.payDate || "").arg(check.periodStart || "").arg(check.periodEnd || "")
                  .arg(String(check.number || "").length > 0 ? qsTr(" · check %1").arg(check.number) : "")
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: mode === "adjust" ? qsTr("Replace paycheck") : mode === "void" ? qsTr("Void paycheck") : qsTr("Close")
    destructive: mode === "void"
    secondaryText: ""
    tertiaryText: mode === "view" ? "" : qsTr("Back")

    function openFor(id) {
        check = payroll.paycheck(id)
        mode = "view"; problem = ""
        open()
    }
    function startAdjust() {
        hours.text = String(check.hours || ""); pay.text = String(check.payText || ""); bonus.text = String(check.bonusText || "")
        payDate.text = String(check.payDate || ""); reason.text = ""
        mode = "adjust"
    }
    // The steps; the tests fill the fields and call them directly.
    function adjust() {
        const corrected = { pay: pay.text, bonus: bonus.text, payDate: payDate.text, reason: reason.text }
        if (String(check.hours || "").length > 0 || hours.text.trim().length > 0) corrected.hours = hours.text
        const r = payroll.adjustPaycheck(check.id, corrected)
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) { check = payroll.paycheck(r.id); mode = "view" }
        return r
    }
    function voidIt() {
        const r = payroll.voidPaycheck(check.id, reason.text.trim().length > 0 ? reason.text : qsTr("Voided"))
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) { check = payroll.paycheck(check.id); mode = "view" }
        return r
    }
    // A queued paycheck is numbered and printed from Checks to Print (M68);
    // a numbered one prints again as it is.
    function printCheck() {
        if (check.toPrint === true) { close(); host.actions.trigger("booksPrintChecksAction"); return }
        host.selectedDocumentId = check.id
        host.actions.trigger("booksPrintCheckAction")
    }
    onAccepted: {
        if (mode === "adjust") { adjust(); open() }
        else if (mode === "void") { voidIt(); open() }
    }
    onTertiary: { mode = "view"; problem = "" }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "paycheckProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not changed"); text: dialog.problem }
        Tk.Notice {
            objectName: "paycheckVoided"
            visible: dialog.check.voided === true
            variant: "warning"
            title: qsTr("Void")
            text: qsTr("This paycheck no longer counts: not in the year to date, the liabilities or the forms.")
        }
        Tk.Flex {
            visible: dialog.mode === "view"
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.KeyValue { key: qsTr("Net pay"); value: String(dialog.check.netText || ""); accent: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "paycheckPrint"; visible: dialog.check.voided !== true; variant: "outline"; iconName: "printer"; text: dialog.check.toPrint === true ? qsTr("Checks to Print…") : qsTr("Print…"); onClicked: dialog.printCheck() }
            Tk.Button { objectName: "paycheckAdjust"; visible: dialog.check.voided !== true; variant: "outline"; iconName: "square-pen"; text: qsTr("Adjust…"); onClicked: dialog.startAdjust() }
            Tk.Button { objectName: "paycheckVoid"; visible: dialog.check.voided !== true; variant: "ghost"; iconName: "circle-x"; text: qsTr("Void…"); onClicked: { reason.text = ""; dialog.mode = "void" } }
        }
        Tk.Grid {
            visible: dialog.mode !== "view"
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { visible: dialog.mode === "adjust"; label: qsTr("Regular pay"); Tk.TextField { id: pay; objectName: "paycheckPay"; small: true; mono: true; horizontalAlignment: Text.AlignRight } }
            Tk.PropertyRow { visible: dialog.mode === "adjust"; label: qsTr("Hours"); Tk.TextField { id: hours; objectName: "paycheckHours"; small: true; mono: true } }
            Tk.PropertyRow { visible: dialog.mode === "adjust"; label: qsTr("Bonus"); Tk.TextField { id: bonus; objectName: "paycheckBonus"; small: true; mono: true; horizontalAlignment: Text.AlignRight } }
            Tk.PropertyRow { visible: dialog.mode === "adjust"; label: qsTr("Pay date"); Tk.TextField { id: payDate; objectName: "paycheckDate"; small: true; mono: true } }
            Tk.PropertyRow { label: qsTr("Reason"); Tk.TextField { id: reason; objectName: "paycheckReason"; small: true; placeholderText: dialog.mode === "void" ? qsTr("Lost check, wrong person…") : qsTr("Hours corrected…") } }
        }
        Tk.Caption {
            visible: dialog.mode !== "view"
            wrapMode: Text.Wrap
            text: dialog.mode === "adjust"
                  ? qsTr("The paycheck is voided and a corrected one written with the same number; the taxes are computed again with the year to date as it stood.")
                  : qsTr("Voiding takes the paycheck out of the books, the year to date, the liabilities and the forms. It stays in the history, marked void.")
        }
        Tk.SectionHeader { title: qsTr("Pay stub") }
        Tk.DataTable {
            objectName: "paycheckStub"
            implicitHeight: Tk.Theme.size.row * ((dialog.check.stub || []).length + 1.5)
            model: dialog.check.stub || []
            columns: [
                Tk.TableColumn { key: "label"; title: qsTr("Item"); flex: 2 },
                Tk.TableColumn { key: "currentText"; title: qsTr("This check"); flex: 1; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "ytdText"; title: qsTr("Year to date"); flex: 1; align: Qt.AlignRight; mono: true }
            ]
        }
        Tk.SectionHeader { title: qsTr("Employer taxes (not on the stub)") }
        Tk.DataTable {
            objectName: "paycheckEmployer"
            implicitHeight: Tk.Theme.size.row * ((dialog.check.employer || []).length + 1.5)
            model: dialog.check.employer || []
            columns: [
                Tk.TableColumn { key: "label"; title: qsTr("Tax"); flex: 2 },
                Tk.TableColumn { key: "currentText"; title: qsTr("This check"); flex: 1; align: Qt.AlignRight; mono: true }
            ]
        }
        Repeater {
            model: dialog.check.notes || []
            delegate: Tk.Caption { required property var modelData; wrapMode: Text.Wrap; text: String(modelData) }
        }
        Tk.SectionHeader { id: federalHeader; objectName: "paycheckFederalHeader"; title: qsTr("Federal withholding — Publication 15-T Worksheet 1A"); collapsible: true; collapsed: true }
        WorksheetLines { objectName: "paycheckFederalWorksheet"; visible: !federalHeader.collapsed; lines: dialog.check.federalWorksheet || [] }
        Tk.SectionHeader { id: coloradoHeader; objectName: "paycheckColoradoHeader"; title: qsTr("Colorado withholding — DR 1098"); collapsible: true; collapsed: true }
        WorksheetLines { objectName: "paycheckColoradoWorksheet"; visible: !coloradoHeader.collapsed; lines: dialog.check.coloradoWorksheet || [] }
    }
}
