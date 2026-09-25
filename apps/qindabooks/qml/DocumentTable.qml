// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A sortable list of documents (invoices, bills, expenses…) as
// BooksService.documents() returns them. Double-click or Enter opens one;
// the status is a coloured badge. Sorting is done here on a copy — the
// service's list is never reordered in place.
Tk.DataTable {
    id: table

    property var rows: []
    property bool showDue: true
    property string partyTitle: qsTr("Customer")
    readonly property var current: currentIndex >= 0 && currentIndex < sortedRows.length ? sortedRows[currentIndex] : null
    readonly property var sortedRows: {
        const key = sortKey || "date"
        const copy = rows.slice()
        const numeric = key === "total" || key === "balance"
        copy.sort(function(a, b) {
            const x = numeric ? Number(a[key] || 0) : String(a[key] || ""), y = numeric ? Number(b[key] || 0) : String(b[key] || "")
            const c = x < y ? -1 : x > y ? 1 : 0
            return sortOrder === Qt.AscendingOrder ? c : -c
        })
        return copy
    }
    signal openRequested(var row)

    model: sortedRows
    sortKey: "date"
    sortOrder: Qt.DescendingOrder
    alternatingRows: true
    emptyText: qsTr("Nothing to show")
    onSortRequested: function(key, order) { table.sortKey = key; table.sortOrder = order }
    onActivated: function(index) { table.openRequested(table.sortedRows[index]) }

    function tone(status) {
        return status === "overdue" ? "danger" : status === "paid" ? "success" : status === "draft" ? "default"
             : status === "void" ? "default" : status === "partial" ? "warning" : "info"
    }

    columns: [
        Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
        Tk.TableColumn { key: "kindLabel"; title: qsTr("Type"); width: Tk.Theme.size.tableColumn * 1.3 },
        Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn; mono: true },
        Tk.TableColumn { key: "partyName"; title: table.partyTitle; flex: 1 },
        Tk.TableColumn { key: "dueDate"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn * 1.2; mono: true; visible: table.showDue },
        Tk.TableColumn { key: "total"; title: qsTr("Total"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true
                         formatter: function(value, row) { return row.totalText } },
        Tk.TableColumn { key: "balance"; title: qsTr("Balance"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true
                         formatter: function(value, row) { return row.balanceText } },
        Tk.TableColumn {
            key: "status"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn * 1.2
            delegate: Item {
                Tk.Badge {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.parent && parent.parent.row ? String(parent.parent.row.statusLabel || "") : ""
                    variant: parent.parent && parent.parent.row ? table.tone(String(parent.parent.row.status)) : "default"
                }
            }
        }
    ]
}
