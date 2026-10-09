// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk

// Mailings ▸ Select Recipients ▸ Use an Existing List (M77, D-086): the
// file (CSV or text, or an Excel workbook through calc_io), and Word's
// Select Table when a workbook has more than one sheet with data.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property url workbook
    property var sheets: []
    objectName: "selectTableDialog"
    title: qsTr("Select Table")
    dialogWidth: 380
    primaryText: qsTr("OK")

    Connections {
        target: dialog.mailMerge
        function onExistingListRequested() { listFile.open() }
        function onSheetRequested(url, sheets) {
            dialog.workbook = url
            dialog.sheets = sheets
            sheet.currentIndex = 0
            dialog.open()
        }
    }
    onAccepted: dialog.mailMerge.useWorkbookSheet(workbook, sheets[sheet.currentIndex])

    Dialogs.FileDialog {
        id: listFile
        title: qsTr("Select Data Source")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Data sources (*.csv *.txt *.tsv *.xlsx *.xlsm)"), qsTr("All files (*)")]
        onAccepted: dialog.mailMerge.useList(selectedFile)
    }

    Tk.PropertyRow {
        label: qsTr("Sheet")
        Tk.ComboBox { id: sheet; objectName: "selectTableSheet"; model: dialog.sheets }
    }
}
