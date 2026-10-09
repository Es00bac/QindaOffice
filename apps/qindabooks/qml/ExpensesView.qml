// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Expenses (D-047): bills, expenses, checks and vendor credits · purchase
// orders (M68: open, partially received, closed) · vendors (with what their
// open orders still have to deliver), each a searchable table; a
// double-click opens the document, the order or the vendor.
Tk.Box {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property int tab: 0
    property string search: ""
    property string status: "all"
    property var rows: []
    readonly property var statuses: tab === 1
        ? [{ text: qsTr("All"), value: "all" }, { text: qsTr("Open"), value: "open" },
           { text: qsTr("Partly received"), value: "partiallyReceived" }, { text: qsTr("Closed"), value: "closed" }]
        : [{ text: qsTr("All"), value: "all" }, { text: qsTr("Unpaid"), value: "open" },
           { text: qsTr("Overdue"), value: "overdue" }, { text: qsTr("Paid"), value: "paid" }]
    property var selectedOrder: null

    color: Tk.Theme.color.surface
    padding: 0

    function refresh() {
        if (books === null || !books.hasCompany) { rows = []; return }
        if (tab <= 1) rows = books.documents({ kind: tab === 0 ? "purchases" : "orders", status: status, search: search })
        else rows = books.parties("vendor", false).filter(function(p) {
            return view.search.length === 0 || (p.name + " " + p.company + " " + p.email).toLowerCase().indexOf(view.search.toLowerCase()) >= 0 })
    }
    onTabChanged: { status = "all"; selectedOrder = null; refresh() }
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
            Tk.Heading { level: 2; text: qsTr("Expenses"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "expensesNew"; variant: "accent"; iconName: "plus"
                        text: view.tab === 0 ? qsTr("New expense") : view.tab === 1 ? qsTr("New purchase order") : qsTr("New vendor")
                        onClicked: view.host.actions.trigger(["booksNewExpenseAction", "booksNewPurchaseOrderAction", "booksNewVendorAction"][view.tab]) }
            Tk.Button { objectName: "expensesCreateBill"; visible: view.tab === 1; variant: "outline"; iconName: "clipboard-check"; text: qsTr("Create bill")
                        enabled: view.selectedOrder !== null && (view.selectedOrder.status === "open" || view.selectedOrder.status === "partiallyReceived")
                        onClicked: view.host.actions.trigger("booksCreateBillFromOrderAction") }
            Tk.Button { objectName: "expensesNewBill"; variant: "outline"; iconName: "file-check"; text: qsTr("New bill"); onClicked: view.host.actions.trigger("booksNewBillAction") }
            Tk.Button { objectName: "expensesPay"; variant: "outline"; iconName: "banknote"; text: qsTr("Pay bills"); onClicked: view.host.actions.trigger("booksPayBillsAction") }
        }
        Tk.TabStrip {
            objectName: "expensesTabs"
            model: [qsTr("Bills & expenses"), qsTr("Purchase orders"), qsTr("Vendors")]
            currentIndex: view.tab
            onTabActivated: function(index) { view.tab = index }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.SearchField { objectName: "expensesSearch"; Tk.Flex.grow: 1; Tk.Flex.basis: 0; placeholderText: qsTr("Search"); onSearched: function(value) { view.search = value } }
            Tk.Segmented {
                objectName: "expensesStatus"
                visible: view.tab <= 1
                small: true
                model: view.statuses
                currentIndex: view.statuses.map(function(s) { return s.value }).indexOf(view.status)
                onActivated: function(index) { view.status = view.statuses[index].value }
            }
        }
        DocumentTable {
            objectName: "expenseDocuments"
            visible: view.tab === 0
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            rows: view.tab === 0 ? view.rows : []
            partyTitle: qsTr("Payee")
            onOpenRequested: function(row) { view.host.documentEditorRequested(String(row.kind), row.id) }
            onCurrentChanged: if (current !== null) { view.host.selectedDocumentId = current.id; view.host.setPrintSource("document", current.id, ({})) }
        }
        // M68: the balance of an order is what it still has to receive.
        DocumentTable {
            objectName: "expenseOrders"
            visible: view.tab === 1
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            rows: view.tab === 1 ? view.rows : []
            partyTitle: qsTr("Vendor")
            dueTitle: qsTr("Expected")
            balanceTitle: qsTr("To receive")
            onOpenRequested: function(row) { view.host.documentEditorRequested(String(row.kind), row.id) }
            onCurrentChanged: if (current !== null) { view.selectedOrder = current; view.host.selectedDocumentId = current.id; view.host.setPrintSource("document", current.id, ({})) }
        }
        Tk.DataTable {
            objectName: "expenseVendors"
            visible: view.tab === 2
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.tab === 2 ? view.rows : []
            alternatingRows: true
            emptyText: qsTr("No vendors yet")
            onActivated: function(index) { view.host.partyDialogRequested("vendor", view.rows[index].id) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "email"; title: qsTr("Email"); flex: 1; muted: true },
                Tk.TableColumn { key: "phone"; title: qsTr("Phone"); width: Tk.Theme.size.tableColumn * 1.5; mono: true },
                Tk.TableColumn { key: "toReceive"; title: qsTr("On order"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.openOrders > 0 ? row.toReceiveText : "—" } },
                Tk.TableColumn { key: "balance"; title: qsTr("You owe"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.balanceText } }
            ]
        }
    }
}
