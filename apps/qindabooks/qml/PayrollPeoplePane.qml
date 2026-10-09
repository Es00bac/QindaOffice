// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Employees and Payroll ▸ Paychecks (M71, D-081). Employees:
// everyone on the M68 Employees list, with their pay and a masked SSN —
// one without payroll set up says so; a double-click opens the payroll
// editor. Paychecks: this year's and last year's, void and adjusted ones
// marked; a double-click opens the paycheck with its stub and worksheets.
Tk.Flex {
    id: pane

    property BooksHost host: null
    property PayrollService payroll: null
    property string mode: "employees"       // employees | paychecks
    property int year: 0
    property var rows: []
    property string search: ""

    signal employeeRequested(var id)
    signal paycheckRequested(var id)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function matches(text) { return search.length === 0 || String(text).toLowerCase().indexOf(search.toLowerCase()) >= 0 }
    function refresh() {
        if (payroll === null) { rows = []; return }
        if (mode === "employees") rows = payroll.employees().filter(function(e) { return pane.matches(e.name) })
        else rows = payroll.paychecks(String(year - 1) + "-01-01", "").filter(function(p) { return pane.matches(p.employeeName + " " + p.number) })
                        .reverse()
    }
    onModeChanged: refresh()
    onSearchChanged: refresh()

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.SearchField { objectName: "payrollPeopleSearch"; Tk.Flex.grow: 1; Tk.Flex.basis: 0; placeholderText: qsTr("Search"); onSearched: function(value) { pane.search = value } }
        Tk.Button {
            objectName: "payrollNewEmployee"
            visible: pane.mode === "employees"
            small: true; variant: "outline"; iconName: "user-plus"; text: qsTr("New employee…")
            onClicked: pane.employeeRequested(0)
        }
        Tk.Button {
            objectName: "payrollRunFromList"
            visible: pane.mode === "paychecks"
            small: true; variant: "outline"; iconName: "receipt"; text: qsTr("Run payroll…")
            onClicked: pane.host.actions.trigger("booksRunPayrollAction")
        }
    }
    Tk.DataTable {
        objectName: "payrollEmployees"
        visible: pane.mode === "employees"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        model: pane.mode === "employees" ? pane.rows : []
        alternatingRows: true
        emptyText: qsTr("No employees yet. Add one, or set up payroll for someone on the Time ▸ Employees list.")
        onActivated: function(index) { pane.employeeRequested(pane.rows[index].id) }
        columns: [
            Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1.4; minWidth: Tk.Theme.size.tableColumn * 1.4 },
            Tk.TableColumn { key: "payTypeText"; title: qsTr("Paid"); width: Tk.Theme.size.tableColumn
                             formatter: function(value, row) { return row.hasPayroll ? String(value) : qsTr("Not set up") } },
            Tk.TableColumn { key: "payText"; title: qsTr("Pay rate"); flex: 1; minWidth: Tk.Theme.size.tableColumn * 1.6; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "frequencyText"; title: qsTr("How often"); flex: 1.2; minWidth: Tk.Theme.size.tableColumn * 1.2; muted: true },
            Tk.TableColumn { key: "ssnMasked"; title: qsTr("SSN"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn { key: "ownerEmployee"; title: qsTr("Role"); width: Tk.Theme.size.tableColumn
                             formatter: function(value, row) { return row.ownerEmployee ? qsTr("Owner") : (row.released ? qsTr("Released") : "") } }
        ]
    }
    Tk.DataTable {
        objectName: "payrollPaychecks"
        visible: pane.mode === "paychecks"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        model: pane.mode === "paychecks" ? pane.rows : []
        alternatingRows: true
        emptyText: qsTr("No paychecks yet. Run payroll to write them.")
        onActivated: function(index) { pane.paycheckRequested(pane.rows[index].id) }
        columns: [
            Tk.TableColumn { key: "date"; title: qsTr("Paid"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn * 0.8; mono: true },
            Tk.TableColumn { key: "employeeName"; title: qsTr("Employee"); flex: 1.4; minWidth: Tk.Theme.size.tableColumn * 1.4 },
            Tk.TableColumn { key: "grossText"; title: qsTr("Gross"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "withheldText"; title: qsTr("Withheld"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "netText"; title: qsTr("Net pay"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "employerText"; title: qsTr("Employer"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "status"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn; muted: true }
        ]
    }
}
