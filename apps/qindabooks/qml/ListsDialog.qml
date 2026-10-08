// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// File ▸ Import and Export ▸ Import Lists… / Export Lists… (M63, D-071):
// the user picks WHICH name list travels — customers, vendors, products
// and services, or the chart of accounts — and then the CSV file. An
// import answers in the dialog itself: what was added, what was already
// there, and every row that was refused with its row number. QuickBooks
// Online and QuickBooks Desktop list exports import here as they are
// (books_io list_import.cpp knows their column names).
//
// AGENT-NOTE: the window used to send the singular "customer" for every
// import and drop the service's answer, so each import failed silently.
// The list is now chosen here and the answer is always shown.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string mode: "import"          // import | export
    property var answer: null               // the service's last answer (T.Dialog owns `result`)
    readonly property var lists: [{ text: qsTr("Customers"), value: "customers" }, { text: qsTr("Vendors"), value: "vendors" },
                                  { text: qsTr("Products & services"), value: "items" }, { text: qsTr("Accounts"), value: "accounts" }]
    readonly property string kind: lists[Math.max(0, kindBox.currentIndex)].value
    readonly property var errors: answer !== null ? (answer.errors || []) : []

    objectName: "listsDialog"
    title: mode === "import" ? qsTr("Import a list") : qsTr("Export a list")
    subtitle: mode === "import" ? qsTr("CSV from QindaBooks, QuickBooks Online, QuickBooks Desktop or a spreadsheet")
                                : qsTr("CSV that QindaBooks, QuickBooks and spreadsheets read")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: mode === "import" ? qsTr("Choose a file…") : qsTr("Save as…")
    secondaryText: qsTr("Close")

    function openFor(m, list) {
        mode = m === "export" ? "export" : "import"
        answer = null
        const i = lists.map(function(l) { return l.value }).indexOf(list || "")
        if (i >= 0) kindBox.currentIndex = i
        open()
    }
    // The file step; the tests call these directly (no native file dialog).
    function importFile(url) { answer = books.importListCsv(kind, url); open() }
    function exportFile(url) { answer = books.exportListCsv(kind, url); open() }
    function summary() {
        if (answer === null) return ""
        if (mode === "export") return answer.ok ? qsTr("Saved to %1").arg(answer.path) : String(answer.error || "")
        if (!answer.ok) return String(answer.error || "")
        return qsTr("%1 added, %2 already in the books (skipped).").arg(answer.added || 0).arg(answer.skipped || 0)
    }
    onAccepted: fileDialog.open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.PropertyRow {
            label: qsTr("List")
            Tk.Segmented {
                id: kindBox
                objectName: "listsKind"
                small: true
                model: dialog.lists
                onActivated: dialog.answer = null
            }
        }
        Tk.Caption {
            visible: dialog.mode === "import"
            wrapMode: Text.Wrap
            text: qsTr("The first row names the columns. Rows whose name is already in the books are skipped, never "
                       + "overwritten; an account's type is a QindaBooks, QuickBooks or IIF type name.")
        }
        Tk.Notice {
            objectName: "listsResult"
            visible: dialog.answer !== null
            variant: dialog.answer === null ? "info" : !dialog.answer.ok ? "danger"
                     : dialog.errors.length > 0 ? "warning" : "success"
            title: dialog.answer === null ? "" : !dialog.answer.ok ? qsTr("Nothing imported")
                   : dialog.mode === "export" ? qsTr("Exported")
                   : dialog.errors.length > 0 ? qsTr("Imported with %n refused row(s)", "", dialog.errors.length) : qsTr("Imported")
            text: dialog.summary()
        }
        Repeater {
            objectName: "listsErrors"
            model: dialog.errors.slice(0, 40)
            delegate: Tk.Caption {
                required property var modelData
                wrapMode: Text.Wrap
                color: Tk.Theme.color.danger
                text: String(modelData)
            }
        }
        Tk.Caption {
            visible: dialog.errors.length > 40
            text: qsTr("…and %n more.", "", dialog.errors.length - 40)
        }
    }
    Dialogs.FileDialog {
        id: fileDialog
        title: dialog.title
        fileMode: dialog.mode === "import" ? Dialogs.FileDialog.OpenFile : Dialogs.FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV (*.csv)"), qsTr("Text (*.txt)"), qsTr("All files (*)")]
        onAccepted: dialog.mode === "import" ? dialog.importFile(selectedFile) : dialog.exportFile(selectedFile)
        onRejected: dialog.open()
    }
}
