// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Accounting (D-045/D-047): the chart of accounts with live balances
// (sub-accounts indented), the journal entries, and the sales-tax rates.
Tk.Box {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property int tab: 0
    property bool showInactive: false
    property var rows: []

    color: Tk.Theme.color.surface
    padding: Tk.Theme.space.lg

    function refresh() {
        if (books === null || !books.hasCompany) { rows = []; return }
        rows = tab === 0 ? books.accounts(showInactive) : tab === 1 ? books.documents({ kind: "journal", status: "all" }) : books.taxRates()
    }
    onTabChanged: refresh()
    onShowInactiveChanged: refresh()
    onVisibleChanged: if (visible) refresh()
    Connections { target: view.books; function onChanged() { view.refresh() } }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Accounting"); Tk.Flex.grow: 1 }
            Tk.Caption { visible: view.books !== null && String(view.books.lockDate).length > 0; text: qsTr("Books closed through %1").arg(view.books !== null ? view.books.lockDate : "") }
            Tk.Button { objectName: "accountsNew"; variant: "accent"; iconName: "plus"
                        text: view.tab === 0 ? qsTr("New account") : view.tab === 1 ? qsTr("Journal entry") : qsTr("New tax rate")
                        onClicked: view.tab === 0 ? view.host.actions.trigger("booksNewAccountAction")
                                 : view.tab === 1 ? view.host.actions.trigger("booksJournalEntryAction") : taxDialog.openFor({}) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.md
            Tk.TabStrip {
                objectName: "accountingTabs"
                Tk.Flex.grow: 1
                model: [qsTr("Chart of accounts"), qsTr("Journal entries"), qsTr("Sales tax")]
                currentIndex: view.tab
                onTabActivated: function(index) { view.tab = index }
            }
            Tk.Switch { visible: view.tab === 0; small: true; text: qsTr("Show inactive"); checked: view.showInactive; onToggled: view.showInactive = checked }
        }
        Tk.DataTable {
            objectName: "chartOfAccounts"
            visible: view.tab === 0
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            model: view.tab === 0 ? view.rows : []
            alternatingRows: true
            onActivated: function(index) { view.host.accountDialogRequested(view.rows[index].id) }
            columns: [
                Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn * 0.8; mono: true },
                Tk.TableColumn { key: "name"; title: qsTr("Account"); flex: 2
                                 formatter: function(value, row) { return "    ".repeat(row.depth || 0) + value + (row.active === false ? qsTr(" (inactive)") : "") } },
                Tk.TableColumn { key: "typeLabel"; title: qsTr("Type"); flex: 1; muted: true },
                Tk.TableColumn { key: "balance"; title: qsTr("Balance"); width: Tk.Theme.size.tableColumn * 1.5; align: Qt.AlignRight; mono: true
                                 formatter: function(value, row) { return row.balanceText } }
            ]
        }
        DocumentTable {
            objectName: "journalEntries"
            visible: view.tab === 1
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            rows: view.tab === 1 ? view.rows : []
            showDue: false
            partyTitle: qsTr("Memo")
            onOpenRequested: function(row) { view.host.documentEditorRequested("journal", row.id) }
        }
        Tk.DataTable {
            objectName: "taxRates"
            visible: view.tab === 2
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            model: view.tab === 2 ? view.rows : []
            emptyText: qsTr("No sales tax rates. Add one if you charge sales tax.")
            onActivated: function(index) { taxDialog.openFor(view.rows[index]) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Rate"); flex: 1 },
                Tk.TableColumn { key: "rateText"; title: qsTr("Percent"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "agencyName"; title: qsTr("Agency"); flex: 1; muted: true }
            ]
        }
    }
    Tk.Dialog {
        id: taxDialog
        property var rate: ({})
        title: Number(rate.id || 0) > 0 ? qsTr("Sales tax rate") : qsTr("New sales tax rate")
        primaryText: qsTr("Save")
        secondaryText: qsTr("Cancel")
        function openFor(r) { rate = r; taxName.text = String(r.name || ""); taxPercent.text = String(r.percent || ""); taxAgency.text = String(r.agencyName || ""); open() }
        onAccepted: view.books.saveTaxRate(Object.assign({}, rate, { name: taxName.text, percent: taxPercent.text, agencyName: taxAgency.text }))
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: taxName; placeholderText: qsTr("e.g. Oregon lodging") } }
            Tk.PropertyRow { label: qsTr("Percent"); Tk.TextField { id: taxPercent; mono: true; placeholderText: "8.25" } }
            Tk.PropertyRow { label: qsTr("Paid to"); Tk.TextField { id: taxAgency; placeholderText: qsTr("Tax agency") } }
        }
    }
}
