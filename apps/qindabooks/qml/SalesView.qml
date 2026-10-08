// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Sales (D-047): invoices, receipts and credits · estimates · customers ·
// products and services, each a searchable table; a double-click opens the
// document or the record. The status filter reads the service's statuses
// (open, overdue, paid, draft, void).
Tk.Box {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property int tab: 0
    property string search: ""
    property string status: "all"
    property var rows: []
    readonly property var statuses: [{ text: qsTr("All"), value: "all" }, { text: qsTr("Open"), value: "open" },
                                     { text: qsTr("Overdue"), value: "overdue" }, { text: qsTr("Paid"), value: "paid" },
                                     { text: qsTr("Draft"), value: "draft" }]

    color: Tk.Theme.color.surface
    padding: 0

    function refresh() {
        if (books === null || !books.hasCompany) { rows = []; return }
        if (tab === 0) rows = books.documents({ kind: "sales", status: status, search: search })
        else if (tab === 1) rows = books.documents({ kind: "estimate", status: status, search: search })
        else if (tab === 2) rows = books.parties("customer", false).filter(function(p) { return view.matches(p.name + " " + p.company + " " + p.email) })
        else rows = books.items(false).filter(function(i) { return view.matches(i.name + " " + i.description) })
    }
    function matches(text) { return search.length === 0 || String(text).toLowerCase().indexOf(search.toLowerCase()) >= 0 }
    function openRow(row) {
        if (row === null || row === undefined) return
        if (tab <= 1) host.documentEditorRequested(String(row.kind), row.id)
        else if (tab === 2) host.partyDialogRequested("customer", row.id)
        else host.itemDialogRequested(row.id)
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
            Tk.Heading { level: 2; text: qsTr("Sales"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "salesNew"; variant: "accent"; iconName: "plus"
                        text: view.tab === 0 ? qsTr("New invoice") : view.tab === 1 ? qsTr("New estimate") : view.tab === 2 ? qsTr("New customer") : qsTr("New product or service")
                        onClicked: view.host.actions.trigger(["booksNewInvoiceAction", "booksNewEstimateAction", "booksNewCustomerAction", "booksNewItemAction"][view.tab]) }
            Tk.Button { objectName: "salesReceive"; variant: "outline"; iconName: "hand-coins"; text: qsTr("Receive payment"); onClicked: view.host.actions.trigger("booksReceivePaymentAction") }
            // M63: the statements and schedules the engine always had.
            Tk.Button { objectName: "salesRecurring"; visible: view.tab === 0; variant: "outline"; iconName: "repeat"; text: qsTr("Recurring…"); onClicked: view.host.actions.trigger("booksRecurringAction") }
            Tk.Button { objectName: "salesStatements"; visible: view.tab === 2; variant: "outline"; iconName: "scroll-text"; text: qsTr("Statements…"); onClicked: view.host.actions.trigger("booksStatementsAction") }
        }
        Tk.TabStrip {
            objectName: "salesTabs"
            model: [qsTr("Invoices & receipts"), qsTr("Estimates"), qsTr("Customers"), qsTr("Products & services")]
            currentIndex: view.tab
            onTabActivated: function(index) { view.tab = index }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.SearchField { objectName: "salesSearch"; Tk.Flex.grow: 1; Tk.Flex.basis: 0; placeholderText: qsTr("Search"); onSearched: function(value) { view.search = value } }
            Tk.Segmented {
                visible: view.tab <= 1
                small: true
                model: view.statuses
                currentIndex: view.statuses.map(function(s) { return s.value }).indexOf(view.status)
                onActivated: function(index) { view.status = view.statuses[index].value }
            }
        }
        DocumentTable {
            objectName: "salesDocuments"
            visible: view.tab <= 1
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            rows: view.tab <= 1 ? view.rows : []
            showDue: view.tab === 0
            onOpenRequested: function(row) { view.openRow(row) }
            onCurrentChanged: if (current !== null) { view.host.selectedDocumentId = current.id; view.host.setPrintSource("document", current.id, ({})) }
        }
        Tk.DataTable {
            objectName: "salesCustomers"
            visible: view.tab === 2
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.tab === 2 ? view.rows : []
            alternatingRows: true
            emptyText: qsTr("No customers yet")
            onActivated: function(index) { view.openRow(view.rows[index]) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "email"; title: qsTr("Email"); flex: 1; muted: true },
                Tk.TableColumn { key: "phone"; title: qsTr("Phone"); width: Tk.Theme.size.tableColumn * 1.5; mono: true },
                Tk.TableColumn { key: "overdueCount"; title: qsTr("Overdue"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight },
                Tk.TableColumn { key: "balance"; title: qsTr("Open balance"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.balanceText } }
            ]
        }
        Tk.DataTable {
            objectName: "salesItems"
            visible: view.tab === 3
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.tab === 3 ? view.rows : []
            alternatingRows: true
            emptyText: qsTr("No products or services yet")
            onActivated: function(index) { view.openRow(view.rows[index]) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "typeLabel"; title: qsTr("Type"); width: Tk.Theme.size.tableColumn * 1.2 },
                Tk.TableColumn { key: "description"; title: qsTr("Description"); flex: 2; muted: true },
                Tk.TableColumn { key: "price"; title: qsTr("Price"); width: Tk.Theme.size.tableColumn * 1.3; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.priceText } }
            ]
        }
    }
}
