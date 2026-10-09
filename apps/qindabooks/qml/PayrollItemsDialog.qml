// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Payroll Tax Items… (D-081 amendment): the payroll taxes of any
// state or city QindaBooks has no built-in tables for, as the business
// enters them — a name, who pays (withheld from the employee, or paid by
// the company), a flat percentage, an optional wage base and annual
// limit, the liability account, the agency and when it is due — and state
// income tax withholding whose rate each employee carries (set in the
// employee's payroll). Paychecks, Pay Payroll Liabilities, the pay stub
// and the payroll reports include them; the built-in forms stay federal
// and Colorado. Nothing here is computed from tables or checked against
// any agency: the numbers are the business's own.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property PayrollService payroll: null
    property var items: []
    property var accounts: []
    property int picked: -1
    property string problem: ""
    readonly property var item: picked >= 0 && picked < items.length ? items[picked] : null
    readonly property bool withholding: kindBox.currentIndex === 1
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var frequencies: ["monthly", "quarterly", "annually"]

    objectName: "payrollItemsDialog"
    title: qsTr("Payroll tax items")
    subtitle: qsTr("Another state's or city's payroll taxes, as the business enters them")
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: qsTr("Save item")
    secondaryText: qsTr("Close")
    tertiaryText: qsTr("New item")

    function indexIn(list, id) { for (let i = 0; i < list.length; ++i) if (Number(list[i].id) === Number(id)) return i; return 0 }
    function openDialog() {
        reload()
        pick(-1)
        problem = ""
        open()
    }
    function reload() {
        items = payroll.payrollItems()
        accounts = [{ id: 0, name: qsTr("Create one under Payroll Liabilities") }].concat(
            books.accounts(false).filter(function(a) { return a.type === "other_current_liability" }))
    }
    function pick(index) {
        picked = index
        const i = item
        nameField.text = i !== null ? String(i.name) : ""
        kindBox.currentIndex = i !== null && i.kind === "withholding" ? 1 : 0
        sideBox.currentIndex = i !== null && i.side === "employee" ? 0 : 1
        rateField.text = i !== null ? String(i.rate) : ""
        baseField.text = i !== null ? String(i.wageBase) : ""
        limitField.text = i !== null ? String(i.annualLimit) : ""
        accountBox.currentIndex = indexIn(accounts, i !== null ? i.accountId : 0)
        agencyField.text = i !== null ? String(i.agency) : ""
        frequencyBox.currentIndex = i !== null ? Math.max(0, frequencies.indexOf(String(i.frequency))) : 1
        dueField.text = i !== null && Number(i.dueDay) > 0 ? String(i.dueDay) : ""
        activeSwitch.checked = i === null || i.active === true
    }
    function collect() {
        return { id: item !== null ? item.id : 0, name: nameField.text,
                 side: withholding || sideBox.currentIndex === 0 ? "employee" : "employer",
                 kind: withholding ? "withholding" : "tax", rate: withholding ? "" : rateField.text,
                 wageBase: baseField.text, annualLimit: limitField.text, accountId: accounts[Math.max(0, accountBox.currentIndex)].id,
                 agency: agencyField.text, frequency: frequencies[Math.max(0, frequencyBox.currentIndex)],
                 dueDay: Number(dueField.text || 0), active: activeSwitch.checked }
    }
    // The save step; the tests fill the fields and call it directly.
    function save() {
        const r = payroll.savePayrollItem(collect())
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) {
            reload()
            for (let i = 0; i < items.length; ++i) if (Number(items[i].id) === Number(r.id)) pick(i)
        }
        return r
    }
    onAccepted: { save(); open() }
    onTertiary: pick(-1)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "payrollItemsProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: dialog.problem }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("Federal taxes and Colorado's are built in. For any other state or city, enter each payroll tax as the agency publishes it: QindaBooks computes it as entered — a percentage of wages up to the wage base — and owes it to the agency on its due dates. State income tax withholding with no built-in table is an item whose percentage or extra amount each employee carries.")
        }
        Tk.DataTable {
            objectName: "payrollItemsTable"
            implicitHeight: Tk.Theme.size.row * (Math.max(2, Math.min(6, dialog.items.length)) + 1.5)
            model: dialog.items
            currentIndex: dialog.picked
            emptyText: qsTr("No payroll tax items yet.")
            onActivated: function(index) { dialog.pick(index) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Item"); flex: 1.6; minWidth: Tk.Theme.size.tableColumn * 1.6 },
                Tk.TableColumn { key: "sideLabel"; title: qsTr("Who pays"); flex: 1.2; minWidth: Tk.Theme.size.tableColumn * 1.4 },
                Tk.TableColumn { key: "rate"; title: qsTr("Rate"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.kind === "withholding" ? qsTr("per employee") : String(value) } },
                Tk.TableColumn { key: "wageBaseText"; title: qsTr("Wage base"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "agency"; title: qsTr("Agency"); flex: 1.2; minWidth: Tk.Theme.size.tableColumn * 1.2 },
                Tk.TableColumn { key: "frequencyLabel"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn },
                Tk.TableColumn { key: "active"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn
                                 formatter: function(value, row) { return row.active ? "" : qsTr("Inactive") } }
            ]
        }
        Tk.SectionHeader { title: dialog.item !== null ? qsTr("Edit %1").arg(dialog.item.name) : qsTr("New item") }
        Tk.Grid {
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "payrollItemName"; small: true; placeholderText: qsTr("State unemployment insurance") } }
            Tk.PropertyRow {
                label: qsTr("Kind")
                Tk.ComboBox { id: kindBox; objectName: "payrollItemKind"; small: true
                              model: [qsTr("Percentage of wages"), qsTr("Withholding, rate set per employee")] }
            }
            Tk.PropertyRow {
                visible: !dialog.withholding; label: qsTr("Who pays")
                Tk.Segmented { id: sideBox; objectName: "payrollItemSide"; small: true; model: [qsTr("Employee"), qsTr("Employer")] }
            }
            Tk.PropertyRow {
                visible: !dialog.withholding; label: qsTr("Rate"); hint: qsTr("A flat percentage of wages, e.g. 2.7%.")
                Tk.TextField { id: rateField; objectName: "payrollItemRate"; small: true; mono: true; horizontalAlignment: Text.AlignRight }
            }
            Tk.PropertyRow {
                label: qsTr("Wage base"); hint: qsTr("Wages taxed per employee each year; empty for no limit.")
                Tk.TextField { id: baseField; objectName: "payrollItemWageBase"; small: true; mono: true; horizontalAlignment: Text.AlignRight }
            }
            Tk.PropertyRow {
                label: qsTr("Annual limit"); hint: qsTr("The most it takes per employee each year; empty for none.")
                Tk.TextField { id: limitField; objectName: "payrollItemLimit"; small: true; mono: true; horizontalAlignment: Text.AlignRight }
            }
            Tk.PropertyRow {
                label: qsTr("Liability account")
                Tk.ComboBox { id: accountBox; objectName: "payrollItemAccount"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow { label: qsTr("Agency"); Tk.TextField { id: agencyField; objectName: "payrollItemAgency"; small: true; placeholderText: qsTr("Who it is paid to") } }
            Tk.PropertyRow {
                label: qsTr("Filed"); Tk.ComboBox { id: frequencyBox; objectName: "payrollItemFrequency"; small: true
                                                    model: [qsTr("Monthly"), qsTr("Quarterly"), qsTr("Annually")] }
            }
            Tk.PropertyRow {
                label: qsTr("Due day"); hint: qsTr("Day of the month after the period; empty for its last day.")
                Tk.TextField { id: dueField; objectName: "payrollItemDueDay"; small: true; mono: true }
            }
            Tk.PropertyRow { label: qsTr("Active"); Tk.Switch { id: activeSwitch; objectName: "payrollItemActive"; small: true } }
        }
    }
}
