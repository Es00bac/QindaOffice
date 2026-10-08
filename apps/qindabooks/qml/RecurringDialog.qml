// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Sales ▸ Recurring Invoices… (M63, D-071): every schedule — customer,
// rhythm, next invoice date, amount, paused or not — with New, Edit,
// Pause/Resume and Delete, and "Create due invoices" for what the
// schedules owe as of today. Editing opens RecurringEditor on top; the
// engine re-bases a changed schedule so no invoice is made twice
// (Recurring::update).
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var rows: []
    property string problem: ""
    readonly property int current: list.currentIndex
    readonly property var selected: current >= 0 && current < rows.length ? rows[current] : null
    readonly property int due: books !== null ? books.dueRecurringCount : 0

    objectName: "recurringDialog"
    title: qsTr("Recurring invoices")
    subtitle: qsTr("Invoices QindaBooks makes on a schedule; nothing is sent until you send it")
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: qsTr("Done")
    secondaryText: ""
    tertiaryText: due > 0 ? qsTr("Create %n due invoice(s)", "", due) : ""

    function openDialog() { problem = ""; refresh(); open() }
    function refresh() { rows = books !== null && books.hasCompany ? books.recurringTemplates() : [] }
    function edit(id) { editor.openFor(id) }
    // Row actions; the tests call them with a schedule id.
    function setActive(id, active) { const r = books.setRecurringActive(id, active); problem = r.ok ? "" : String(r.error || ""); refresh() }
    function remove(id) { const r = books.deleteRecurring(id); problem = r.ok ? "" : String(r.error || ""); list.currentIndex = -1; refresh() }
    function generate() { books.generateDueRecurring(); refresh() }
    onTertiary: generate()
    Connections { target: dialog.books; function onChanged() { if (dialog.opened) dialog.refresh() } }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "recurringProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not done"); text: dialog.problem }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "recurringNew"; small: true; variant: "accent"; iconName: "plus"; text: qsTr("New schedule…"); onClicked: dialog.edit(0) }
            Tk.Button { objectName: "recurringEdit"; small: true; variant: "outline"; iconName: "pencil"; text: qsTr("Edit…")
                        enabled: dialog.selected !== null; onClicked: dialog.edit(dialog.selected.id) }
            Tk.Button { objectName: "recurringPause"; small: true; variant: "outline"
                        iconName: dialog.selected !== null && !dialog.selected.active ? "play" : "pause"
                        text: dialog.selected !== null && !dialog.selected.active ? qsTr("Resume") : qsTr("Pause")
                        enabled: dialog.selected !== null
                        onClicked: dialog.setActive(dialog.selected.id, !dialog.selected.active) }
            Tk.Button { objectName: "recurringDelete"; small: true; variant: "ghost"; iconName: "trash-2"; text: qsTr("Delete…")
                        enabled: dialog.selected !== null; onClicked: confirm.openFor(dialog.selected) }
        }
        Tk.DataTable {
            id: list
            objectName: "recurringList"
            implicitHeight: Tk.Theme.size.panelMinHeight * 2.4
            model: dialog.rows
            alternatingRows: true
            emptyText: qsTr("No recurring invoices yet")
            onActivated: function(index) { dialog.edit(dialog.rows[index].id) }
            columns: [
                Tk.TableColumn { key: "name"; title: qsTr("Schedule"); flex: 1 },
                Tk.TableColumn { key: "customerName"; title: qsTr("Customer"); flex: 1 },
                Tk.TableColumn { key: "scheduleText"; title: qsTr("Repeats"); width: Tk.Theme.size.tableColumn * 1.5 },
                Tk.TableColumn { key: "statusText"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn * 1.6; mono: true },
                Tk.TableColumn { key: "totalText"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.3; align: Qt.AlignRight; mono: true }
            ]
        }
    }
    RecurringEditor { id: editor; books: dialog.books }
    Tk.Dialog {
        id: confirm
        property var row: null
        objectName: "recurringConfirmDelete"
        title: qsTr("Delete this schedule?")
        subtitle: row !== null ? qsTr("“%1” makes no more invoices. The %n invoice(s) it made stay in the books.", "",
                                      row.generated || 0).arg(row.name) : ""
        primaryText: qsTr("Delete")
        destructive: true
        secondaryText: qsTr("Cancel")
        function openFor(r) { row = r; open() }
        onAccepted: dialog.remove(row.id)
    }
}
