// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// File ▸ Import and Export ▸ Import from QuickBooks (IIF)… (M63, D-071):
// moving the books over from QuickBooks Desktop. Choosing a file runs the
// whole import and rolls it back (BooksService.previewIif), so what is
// shown is exactly what Import will do: the lists added or already here,
// each transaction type as documents or journal entries, what is already in
// the books, the accounts the company lacks (map each to one of its own),
// and every refused record with its line numbers. Import then makes a
// backup copy and posts everything as one batch.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property url fileUrl: ""
    property var preview: null       // previewIif() answer
    property var answer: null        // importIif() answer (T.Dialog owns `result`)
    property var mapping: ({})       // QuickBooks account name → account id
    property var accounts: []
    readonly property var refused: preview !== null ? (preview.refused || []) : []
    readonly property var unmapped: preview !== null ? (preview.unmapped || []) : []
    readonly property bool done: answer !== null && answer.committed === true
    // After the import, the tables show what it did rather than the preview.
    readonly property var shown: done ? answer : preview

    objectName: "iifImportDialog"
    title: qsTr("Import from QuickBooks")
    subtitle: qsTr("QuickBooks Desktop's list export (File ▸ Utilities ▸ Export ▸ Lists to IIF Files), transactions from "
                   + "an accountant or payroll service, or a QindaBooks export")
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: done ? qsTr("Done") : preview === null ? qsTr("Choose an IIF file…") : qsTr("Import")
    primaryEnabled: done || preview === null || (preview.ok === true && (refused.length === 0 || skipBox.checked))
    secondaryText: done ? "" : qsTr("Cancel")
    tertiaryText: preview !== null && !done ? qsTr("Another file…") : ""

    function openDialog() { fileUrl = ""; preview = null; answer = null; mapping = ({}); open() }
    function options() { return { accountMap: mapping, autoApply: applyBox.checked, skipRefused: skipBox.checked } }
    // The file and import steps; the tests call them directly.
    function choose(url) {
        fileUrl = url
        accounts = [{ id: 0, fullName: qsTr("Choose an account") }].concat(books.accounts(false))
        refresh()
        open()
    }
    function refresh() { answer = null; preview = books.previewIif(fileUrl, options()) }
    function map(name, accountId) {
        const next = Object.assign({}, mapping)
        if (accountId > 0) next[name] = accountId
        else delete next[name]
        mapping = next
        refresh()
    }
    function importNow() { answer = books.importIif(fileUrl, options()); return answer }
    function readyText(summary) {
        let parts = []
        if (summary.posted > 0) parts.push(qsTr("%n transaction(s) ready.", "", summary.posted))
        if (summary.timeAdded === 1) parts.push(qsTr("One time activity ready."))
        else if (summary.timeAdded > 1) parts.push(qsTr("%1 time activities ready.").arg(summary.timeAdded))
        return parts.length ? parts.join(" ") : qsTr("Lists ready.")
    }
    function importedText(summary) {
        let parts = []
        if (summary.posted > 0)
            parts.push(qsTr("%n transaction(s) posted (%1 of debits).", "", summary.posted).arg(summary.postedText))
        if (summary.timeAdded === 1) parts.push(qsTr("One time activity imported."))
        else if (summary.timeAdded > 1) parts.push(qsTr("%1 time activities imported.").arg(summary.timeAdded))
        if (!parts.length) parts.push(qsTr("Lists imported."))
        parts.push(qsTr("The books as they were before are saved at %1.").arg(summary.backupPath))
        return parts.join(" ")
    }
    onAccepted: {
        if (done) return
        if (preview === null) { fileDialog.open(); return }
        importNow()
        open()
    }
    onTertiary: fileDialog.open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.EmptyState {
            visible: dialog.preview === null
            iconName: "import"
            title: qsTr("Bring your QuickBooks books over")
            text: qsTr("Nothing is changed until you choose Import: first you see what the file will add, and what it cannot.")
        }
        Tk.Notice {
            objectName: "iifResult"
            visible: dialog.answer !== null
            variant: dialog.done ? "success" : "danger"
            title: dialog.done ? qsTr("Imported") : qsTr("Nothing imported")
            text: dialog.answer === null ? "" : dialog.done
                  ? dialog.importedText(dialog.answer)
                  : String(dialog.answer.error || "")
        }
        Tk.Notice {
            objectName: "iifSummary"
            visible: dialog.preview !== null && dialog.answer === null
            variant: dialog.preview !== null && dialog.preview.ok !== true ? "danger" : dialog.refused.length > 0 ? "warning" : "info"
            title: dialog.preview !== null && dialog.preview.ok !== true ? qsTr("This file cannot be imported")
                   : qsTr("Ready to import QuickBooks data")
            text: dialog.preview === null ? "" : dialog.preview.ok !== true ? String(dialog.preview.error || "")
                  : dialog.refused.length > 0 ? qsTr("%n record(s) cannot be imported as they are; see below.", "", dialog.refused.length)
                  : dialog.readyText(dialog.preview)
        }
        Tk.Grid {
            visible: dialog.preview !== null && dialog.preview.ok === true
            columns: dialog.width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "2fr 3fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.lg
            Tk.DataTable {
                objectName: "iifLists"
                implicitHeight: Tk.Theme.size.row * 9
                model: dialog.shown !== null ? (dialog.shown.lists || []) : []
                columns: [
                    Tk.TableColumn { key: "label"; title: qsTr("List"); flex: 1 },
                    Tk.TableColumn { key: "added"; title: qsTr("New"); width: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "existing"; title: qsTr("In books"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "skipped"; title: qsTr("Left out"); width: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true }
                ]
            }
            Tk.DataTable {
                objectName: "iifTransactions"
                implicitHeight: dialog.preview !== null && dialog.preview.posted === 0
                                ? Tk.Theme.size.row * 3 : Tk.Theme.size.row * 7
                model: dialog.shown !== null ? (dialog.shown.transactions || []) : []
                emptyText: qsTr("No transactions in this file")
                columns: [
                    Tk.TableColumn { key: "type"; title: qsTr("QuickBooks type"); flex: 1 },
                    Tk.TableColumn { key: "count"; title: qsTr("In file"); width: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "documents"; title: qsTr("Documents"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "journal"; title: qsTr("Journal"); width: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "duplicates"; title: qsTr("Already in"); width: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "refused"; title: qsTr("Refused"); width: Tk.Theme.size.tableColumn * 0.8; align: Qt.AlignRight; mono: true }
                ]
            }
        }
        Tk.SectionHeader { visible: dialog.unmapped.length > 0; title: qsTr("Accounts this company does not have") }
        Repeater {
            objectName: "iifUnmapped"
            model: dialog.unmapped
            delegate: Tk.Flex {
                id: unknownRow
                required property var modelData
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label { text: String(unknownRow.modelData.name); Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight }
                Tk.Caption { text: qsTr("lines %1").arg(unknownRow.modelData.linesText); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.6; Tk.Flex.shrink: 0; elide: Text.ElideRight }
                Tk.ComboBox {
                    objectName: "iifMap_" + unknownRow.modelData.name
                    small: true
                    implicitWidth: Tk.Theme.size.fieldWidth * 1.6
                    textRole: "fullName"
                    model: dialog.accounts
                    currentIndex: Math.max(0, dialog.accounts.map(function(a) { return a.id }).indexOf(dialog.mapping[unknownRow.modelData.name] || 0))
                    onActivated: function(i) { dialog.map(unknownRow.modelData.name, dialog.accounts[i].id) }
                }
            }
        }
        Tk.Caption { visible: dialog.unmapped.length > 0; wrapMode: Text.Wrap; text: qsTr("Map each one to an account here, or add it to the chart of accounts and choose the file again.") }
        Tk.SectionHeader { visible: dialog.refused.length > 0; title: qsTr("Refused records") }
        Repeater {
            objectName: "iifRefused"
            model: dialog.refused.slice(0, 30)
            delegate: Tk.Caption {
                required property var modelData
                wrapMode: Text.Wrap
                color: Tk.Theme.color.danger
                text: String(modelData.type || "") + " — " + String(modelData.reason)
            }
        }
        Tk.Caption { visible: dialog.refused.length > 30; text: qsTr("…and %n more.", "", dialog.refused.length - 30) }
        Repeater {
            model: dialog.preview !== null ? (dialog.preview.notes || []) : []
            delegate: Tk.Caption {
                required property var modelData
                wrapMode: Text.Wrap
                text: "• " + String(modelData)
            }
        }
        Tk.Flex {
            visible: dialog.preview !== null && !dialog.done
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Switch {
                id: applyBox
                objectName: "iifAutoApply"
                checked: true
                text: qsTr("Apply payments and credits to the oldest open invoices and bills (IIF does not say which)")
                onToggled: dialog.refresh()
            }
            Tk.CheckBox {
                id: skipBox
                objectName: "iifSkipRefused"
                visible: dialog.refused.length > 0
                text: qsTr("Import the rest and leave out the %n refused record(s)", "", dialog.refused.length)
            }
        }
    }
    Dialogs.FileDialog {
        id: fileDialog
        title: qsTr("Choose a QuickBooks IIF file")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("QuickBooks IIF (*.iif *.IIF)"), qsTr("Text (*.txt)"), qsTr("All files (*)")]
        onAccepted: dialog.choose(selectedFile)
        onRejected: dialog.open()
    }
}
