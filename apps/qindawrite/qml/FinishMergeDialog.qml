// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk

// Mailings ▸ Finish & Merge (M77, D-086): Edit Individual Documents (a new
// window, a section per record), Print Documents, Merge to PDF — all the
// records, the one being previewed, or a range — as Word's Merge to New
// Document / Merge to Printer ask.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property string target: "document"
    objectName: "finishMergeDialog"
    title: target === "printer" ? qsTr("Merge to Printer") : target === "pdf" ? qsTr("Merge to PDF")
                                                                               : qsTr("Merge to New Document")
    dialogWidth: 400
    primaryText: qsTr("OK")

    function range() {
        return { records: all.checked ? "all" : current.checked ? "current" : "range",
                 from: fromField.value, to: toField.value }
    }
    Connections {
        target: dialog.mailMerge
        function onFinishRequested(target) {
            if (target === "email") return
            dialog.target = target
            all.checked = true
            current.checked = false
            some.checked = false
            fromField.value = 1
            toField.value = Math.max(1, dialog.mailMerge.recipientCount)
            dialog.open()
        }
    }
    onAccepted: {
        if (target === "pdf") pdfFile.open()
        else if (target === "printer") dialog.mailMerge.finishToPrinter(range())
        else dialog.mailMerge.finishToDocument(range())
    }

    Dialogs.FileDialog {
        id: pdfFile
        title: qsTr("Merge to PDF")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: dialog.mailMerge.finishToPdf(selectedFile, dialog.range())
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Overline { text: qsTr("Merge records") }
        Tk.RadioButton { id: all; objectName: "finishAll"; text: qsTr("All (%1)").arg(dialog.mailMerge.recipientCount); onClicked: { current.checked = false; some.checked = false } }
        Tk.RadioButton { id: current; objectName: "finishCurrent"; text: qsTr("Current record (%1)").arg(dialog.mailMerge.previewRecord); onClicked: { all.checked = false; some.checked = false } }
        Tk.Flex {
            gap: Tk.Theme.space.xs
            Tk.RadioButton { id: some; objectName: "finishRange"; text: qsTr("From"); onClicked: { all.checked = false; current.checked = false } }
            Tk.NumberField { id: fromField; objectName: "finishFrom"; enabled: some.checked; implicitWidth: 70; from: 1; to: Math.max(1, dialog.mailMerge.recipientCount); decimals: 0; onValueModified: function(v) { value = v } }
            Tk.Label { text: qsTr("To") }
            Tk.NumberField { id: toField; objectName: "finishTo"; enabled: some.checked; implicitWidth: 70; from: 1; to: Math.max(1, dialog.mailMerge.recipientCount); decimals: 0; onValueModified: function(v) { value = v } }
        }
    }
}
