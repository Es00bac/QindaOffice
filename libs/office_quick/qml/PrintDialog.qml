// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// File ▸ Print… without QPrintDialog: printer, copies, page range and
// orientation into a PrintJob, then job.print(). `printed(ok)` reports.
Tk.Dialog {
    id: dialog

    property PrintJob job: null

    signal printed(bool ok)

    title: qsTr("Print")
    primaryText: qsTr("Print")
    secondaryText: qsTr("Cancel")
    dialogWidth: 440
    primaryEnabled: printers.count > 0 && job !== null

    PrinterModel { id: printers }

    onOpened: {
        printers.refresh()
        printerBox.currentIndex = Math.max(0, printers.defaultIndex)
        if (job !== null) {
            copies.value = job.copies
            landscape.currentIndex = job.landscape ? 1 : 0
            range.currentIndex = job.fromPage > 0 ? 1 : 0
            fromPage.value = Math.max(1, job.fromPage)
            toPage.value = Math.max(fromPage.value, job.toPage)
        }
    }
    onAccepted: {
        if (job === null) {
            return
        }
        job.printerName = printers.nameAt(printerBox.currentIndex)
        job.copies = copies.value
        job.landscape = landscape.currentIndex === 1
        job.fromPage = range.currentIndex === 1 ? fromPage.value : 0
        job.toPage = range.currentIndex === 1 ? toPage.value : 0
        dialog.printed(job.print())
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Printer")
            Tk.ComboBox { id: printerBox; objectName: "printPrinter"; model: printers; textRole: "name" }
        }
        Tk.EmptyState {
            visible: printers.count === 0
            iconName: "printer"
            title: qsTr("No printers")
            text: qsTr("No printer is configured on this system. Export a PDF instead.")
        }
        Tk.PropertyRow { label: qsTr("Copies"); Tk.NumberField { id: copies; objectName: "printCopies"; from: 1; to: 99; value: 1 } }
        Tk.PropertyRow {
            label: qsTr("Pages")
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Segmented { id: range; objectName: "printRange"; model: [qsTr("All"), qsTr("Range")] }
                Tk.NumberField { id: fromPage; objectName: "printFrom"; from: 1; to: 9999; value: 1; label: qsTr("from"); enabled: range.currentIndex === 1 }
                Tk.NumberField { id: toPage; objectName: "printTo"; from: 1; to: 9999; value: 1; label: qsTr("to"); enabled: range.currentIndex === 1 }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented { id: landscape; objectName: "printOrientation"; model: [qsTr("Portrait"), qsTr("Landscape")] }
        }
    }
}
