// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Time (M68, D-077): time activities — who worked how long for which
// customer:job, billable and billed — and the employees time is entered
// for. A double-click opens the activity or the employee; Time Activity and
// Weekly Timesheet enter time. Billable time reaches invoices from the
// invoice editor; the time reports are under Reports.
Tk.Box {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property int tab: 0
    property string search: ""
    property string status: "all"
    property var rows: []
    readonly property var statuses: [{ text: qsTr("All"), value: "all" }, { text: qsTr("Unbilled"), value: "unbilled" },
                                     { text: qsTr("Billed"), value: "billed" }, { text: qsTr("Not billable"), value: "notBillable" }]

    color: Tk.Theme.color.surface
    padding: 0

    function matches(text) { return search.length === 0 || String(text).toLowerCase().indexOf(search.toLowerCase()) >= 0 }
    function refresh() {
        if (books === null || !books.hasCompany) { rows = []; return }
        if (tab === 0) rows = books.timeEntries({ status: status }).filter(function(e) {
            return view.matches(e.workerName + " " + e.customerName + " " + e.itemName + " " + e.notes) })
        else rows = books.employees(true).filter(function(e) { return view.matches(e.name + " " + e.email) })
    }
    onTabChanged: refresh()
    onStatusChanged: refresh()
    onSearchChanged: refresh()
    onVisibleChanged: if (visible) refresh()
    Connections { target: view.books; function onChanged() { view.refresh() } }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Time"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "timeNew"; variant: "accent"; iconName: view.tab === 0 ? "clock" : "user-round"
                        text: view.tab === 0 ? qsTr("Time activity") : qsTr("New employee")
                        onClicked: view.host.actions.trigger(view.tab === 0 ? "booksTimeActivityAction" : "booksNewEmployeeAction") }
            Tk.Button { objectName: "timeWeekly"; variant: "outline"; iconName: "calendar-days"; text: qsTr("Weekly timesheet")
                        onClicked: view.host.actions.trigger("booksWeeklyTimesheetAction") }
        }
        Tk.TabStrip {
            objectName: "timeTabs"
            model: [qsTr("Time activities"), qsTr("Employees")]
            currentIndex: view.tab
            onTabActivated: function(index) { view.tab = index }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.SearchField { objectName: "timeSearch"; Tk.Flex.grow: 1; Tk.Flex.basis: 0; placeholderText: qsTr("Search"); onSearched: function(value) { view.search = value } }
            Tk.Segmented {
                objectName: "timeStatus"
                visible: view.tab === 0
                small: true
                model: view.statuses
                currentIndex: view.statuses.map(function(s) { return s.value }).indexOf(view.status)
                onActivated: function(index) { view.status = view.statuses[index].value }
            }
        }
        Tk.DataTable {
            objectName: "timeEntries"
            visible: view.tab === 0
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.tab === 0 ? view.rows : []
            alternatingRows: true
            emptyText: qsTr("No time yet. Enter a time activity or a weekly timesheet.")
            onActivated: function(index) { view.host.timeActivityRequested(view.rows[index].id) }
            columns: [
                Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                Tk.TableColumn { key: "workerName"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "customerName"; title: qsTr("Customer:job"); flex: 1 },
                Tk.TableColumn { key: "itemName"; title: qsTr("Service"); width: Tk.Theme.size.tableColumn * 1.4 },
                Tk.TableColumn { key: "minutes"; title: qsTr("Duration"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.duration } },
                Tk.TableColumn {
                    key: "status"; title: qsTr("Billing"); width: Tk.Theme.size.tableColumn * 1.3
                    delegate: Item {
                        Tk.Badge {
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.parent && parent.parent.row ? String(parent.parent.row.statusLabel || "") : ""
                            variant: parent.parent && parent.parent.row ? (parent.parent.row.status === "billed" ? "success"
                                     : parent.parent.row.status === "unbilled" ? "warning" : "default") : "default"
                        }
                    }
                },
                Tk.TableColumn { key: "notes"; title: qsTr("Notes"); flex: 1; muted: true }
            ]
        }
        Tk.DataTable {
            objectName: "timeEmployees"
            visible: view.tab === 1
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.tab === 1 ? view.rows : []
            alternatingRows: true
            emptyText: qsTr("No employees yet. Contractors are your vendors.")
            onActivated: function(index) { view.host.employeeDialogRequested(view.rows[index].id) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "email"; title: qsTr("Email"); flex: 1; muted: true },
                Tk.TableColumn { key: "phone"; title: qsTr("Phone"); width: Tk.Theme.size.tableColumn * 1.5; mono: true },
                Tk.TableColumn { key: "rate"; title: qsTr("Hourly rate"); width: Tk.Theme.size.tableColumn * 1.3; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.rateText || "—" } },
                Tk.TableColumn { key: "active"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn
                                 formatter: function(value, row) { return row.active ? qsTr("Active") : qsTr("Inactive") } }
            ]
        }
    }
}
