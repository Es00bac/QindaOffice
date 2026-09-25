// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Reconciling one account against a statement (D-045): tick every line
// that appears on the statement until the difference is zero, then finish.
// The engine refuses to finish at any other difference; cancelling leaves
// nothing marked.
Tk.Flex {
    id: view

    property BooksService books: null
    property int sessionId: 0
    property string accountName: ""
    property var rows: []
    property var recon: ({})
    signal done()

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.md

    function refresh() {
        if (books === null || sessionId === 0) return
        rows = books.reconcileCandidates(sessionId)
        recon = books.reconcileState(sessionId)
    }
    function toggle(row) { books.markCleared(sessionId, [row.lineId], !row.cleared); refresh() }
    onSessionIdChanged: refresh()

    Tk.Flex {
        direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.lg
        Tk.Heading { level: 2; text: qsTr("Reconcile %1").arg(view.accountName); Tk.Flex.grow: 1 }
        Tk.KeyValue { key: qsTr("Statement ending balance"); value: String(view.recon.endingBalanceText || "") }
        Tk.KeyValue { key: qsTr("Cleared balance"); value: String(view.recon.clearedBalanceText || "") }
        Tk.Box {
            objectName: "reconcileDifference"
            color: view.recon.canFinish ? Tk.Theme.color.accentSubtle : Tk.Theme.color.dangerSubtle
            radius: Tk.Theme.radius.md
            padding: Tk.Theme.space.sm
            Tk.Flex { direction: Tk.Flex.Column
                Tk.Overline { title: qsTr("Difference") }
                Tk.Heading { level: 2; text: String(view.recon.differenceText || "") } }
        }
    }
    Tk.DataTable {
        objectName: "reconcileLines"
        Tk.Flex.grow: 1; Tk.Flex.basis: 0
        model: view.rows
        alternatingRows: true
        emptyText: qsTr("No uncleared transactions up to the statement date")
        onActivated: function(index) { view.toggle(view.rows[index]) }
        columns: [
            Tk.TableColumn {
                key: "cleared"; title: ""; width: Tk.Theme.size.control
                delegate: Tk.CheckBox {
                    readonly property var line: parent ? parent.row : null
                    checked: line !== null && line.cleared === true
                    tooltip: qsTr("On the statement")
                    onToggled: view.toggle(line)
                }
            },
            Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn * 0.8; mono: true },
            Tk.TableColumn { key: "payee"; title: qsTr("Payee"); flex: 1 },
            Tk.TableColumn { key: "amount"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true
                             formatter: function(value, row) { return row.amountText } }
        ]
    }
    Tk.Flex {
        direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
        Tk.Caption { text: view.recon.canFinish ? qsTr("Everything matches the statement.") : qsTr("Keep ticking lines until the difference is zero."); Tk.Flex.grow: 1 }
        Tk.Button { objectName: "reconcileCancel"; variant: "ghost"; text: qsTr("Cancel"); onClicked: { view.books.cancelReconciliation(view.sessionId); view.done() } }
        Tk.Button { objectName: "reconcileFinish"; variant: "accent"; iconName: "check"; text: qsTr("Finish");
                    available: view.recon.canFinish === true; onClicked: { const r = view.books.finishReconciliation(view.sessionId); if (r.ok) view.done() } }
    }
}
