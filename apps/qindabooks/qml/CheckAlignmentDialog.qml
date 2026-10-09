// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Check printer alignment (M68, D-077), QuickBooks' Printer Setup ▸ Align:
// per printer, how far every check field moves right and down (inches to
// the hundredth, or millimetres to the tenth; negative moves left and up),
// and the alignment test page to check it on plain paper held over the
// check stock. Kept per user, per printer name (check_alignment.h).
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property var printers: []          // checkPrinters()
    property string printer: ""
    property string unit: "in"
    property string description: ""
    readonly property var units: [{ text: qsTr("Inches"), value: "in" }, { text: qsTr("Millimetres"), value: "mm" }]

    objectName: "checkAlignmentDialog"
    title: qsTr("Check printer alignment")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Save")
    secondaryText: qsTr("Close")
    primaryEnabled: printer.trim().length > 0

    function openDialog() {
        printers = books.checkPrinters()
        choose(printers.length > 0 ? String(printers[0].name) : "")
        open()
    }
    function choose(name) {
        printer = name
        const a = books.checkAlignment(name)
        unit = String(a.unit || "in")
        xField.text = String(a.x || "0")
        yField.text = String(a.y || "0")
        description = String(a.description || "")
        notice.visible = false
    }
    function save() {
        const r = books.saveCheckAlignment({ printer: printer, x: xField.text, y: yField.text, unit: unit })
        notice.variant = r.ok ? "success" : "danger"
        notice.text = r.ok ? qsTr("Saved: %1.").arg(r.description) : String(r.error || "")
        notice.visible = true
        if (r.ok) description = String(r.description)
        return r.ok
    }
    function printTestPage() {
        if (!save()) return
        host.setPrintSource("alignment", printer, ({}))
        host.actions.trigger("filePrintAction")
    }
    onAccepted: { save(); open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: notice; objectName: "alignmentNotice"; visible: false }
        Tk.PropertyRow {
            label: qsTr("Printer")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox { objectName: "alignmentPrinter"; visible: dialog.printers.length > 0; Tk.Flex.grow: 1; small: true; textRole: "name"; model: dialog.printers
                              onActivated: function(i) { dialog.choose(String(dialog.printers[i].name)) } }
                Tk.TextField { objectName: "alignmentPrinterName"; visible: dialog.printers.length === 0; Tk.Flex.grow: 1; small: true
                               placeholderText: qsTr("The printer's name, as the print dialog lists it")
                               text: dialog.printer; onEditingFinished: dialog.printer = text.trim() }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Units")
            Tk.Segmented { objectName: "alignmentUnit"; small: true; model: dialog.units
                           currentIndex: dialog.units.map(function(u) { return u.value }).indexOf(dialog.unit)
                           onActivated: function(i) { dialog.unit = dialog.units[i].value } }
        }
        Tk.PropertyRow {
            label: qsTr("Move right")
            Tk.TextField { id: xField; objectName: "alignmentX"; small: true; mono: true; placeholderText: "0.00"
                           tooltip: qsTr("A negative value moves every field left") }
        }
        Tk.PropertyRow {
            label: qsTr("Move down")
            Tk.TextField { id: yField; objectName: "alignmentY"; small: true; mono: true; placeholderText: "0.00"
                           tooltip: qsTr("A negative value moves every field up") }
        }
        Tk.Caption {
            text: qsTr("Now: %1. Print the test page on plain paper, hold it over a sheet of check stock against the light, and move the fields until they sit on their lines.").arg(dialog.description || qsTr("none"))
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "alignmentPrint"; small: true; variant: "outline"; iconName: "printer"; text: qsTr("Print test page"); enabled: dialog.printer.length > 0; onClicked: dialog.printTestPage() }
            Tk.Button { objectName: "alignmentPdf"; small: true; variant: "ghost"; iconName: "file-down"; text: qsTr("Save test page as PDF…"); onClicked: pdfDialog.open() }
        }
    }
    Dialogs.FileDialog {
        id: pdfDialog
        title: qsTr("Save the test page as PDF")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: dialog.books.exportAlignmentTestPdf(dialog.printer, selectedFile)
    }
}
