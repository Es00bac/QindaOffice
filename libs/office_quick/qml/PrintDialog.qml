// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// File ▸ Print… without QPrintDialog: printer, copies, page range and
// orientation into a PrintJob, then job.print(). `printed(ok)` reports.
Tk.Dialog {
    id: dialog
    objectName: "printDialog"

    property PrintJob job: null
    property bool initializing: false

    signal printed(bool ok)

    title: qsTr("Print")
    primaryText: qsTr("Print")
    secondaryText: qsTr("Cancel")
    primaryEnabled: printers.count > 0 && printers.nameAt(printerBox.currentIndex).length > 0 && job !== null
                    && (range.currentIndex === 0 || toPage.value >= fromPage.value)

    PrinterModel { id: printers; objectName: "printPrinters" }
    Connections {
        target: dialog.job
        function onSettingsChanged() {
            landscape.currentIndex = dialog.job.landscape ? 1 : 0
        }
    }

    function syncRange() {
        if (!opened || initializing || job === null) return
        job.fromPage = range.currentIndex === 1 ? fromPage.value : 0
        job.toPage = range.currentIndex === 1 ? toPage.value : 0
    }

    function refreshPrinters(preferredName) {
        printers.refresh()
        let selected = -1
        for (let row = 0; row < printers.count; ++row) {
            if (printers.nameAt(row) === preferredName) selected = row
        }
        // A disappearing selection must not silently turn into another printer.
        printerBox.currentIndex = preferredName.length > 0 ? selected
            : printers.count > 0 ? Math.max(0, printers.defaultIndex) : -1
    }

    onOpened: {
        initializing = true
        refreshPrinters(job !== null ? job.printerName : "")
        if (job !== null) {
            copies.value = job.copies
            landscape.currentIndex = job.landscape ? 1 : 0
            range.currentIndex = job.fromPage > 0 ? 1 : 0
            fromPage.value = Math.max(1, job.fromPage)
            toPage.value = Math.max(fromPage.value, job.toPage)
        }
        initializing = false
    }
    onAccepted: {
        if (!primaryEnabled) {
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
            objectName: "printNoPrinters"
            visible: printers.count === 0
            iconName: "printer"
            title: qsTr("No printers")
            text: qsTr("Connect and switch on your printer, then add it in system printer settings. Refresh this list after setup. You can also export a PDF.")
        }
        Tk.Button {
            objectName: "printRefresh"
            text: qsTr("Refresh printers")
            iconName: "refresh-cw"
            onClicked: dialog.refreshPrinters(printers.nameAt(printerBox.currentIndex))
        }
        Tk.PropertyRow { label: qsTr("Copies"); Tk.NumberField { id: copies; objectName: "printCopies"; from: 1; to: 99; value: 1 } }
        Tk.PropertyRow {
            label: qsTr("Pages")
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Segmented { id: range; objectName: "printRange"; model: [qsTr("All"), qsTr("Range")]; onCurrentIndexChanged: dialog.syncRange() }
                Tk.NumberField { id: fromPage; objectName: "printFrom"; from: 1; to: 9999; value: 1; label: qsTr("from"); enabled: range.currentIndex === 1; onValueChanged: dialog.syncRange() }
                Tk.NumberField { id: toPage; objectName: "printTo"; from: 1; to: 9999; value: 1; label: qsTr("to"); enabled: range.currentIndex === 1; onValueChanged: dialog.syncRange() }
            }
        }
        Tk.Caption {
            objectName: "printRangeError"
            visible: range.currentIndex === 1 && toPage.value < fromPage.value
            text: qsTr("The last page must be at least the first page.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented {
                id: landscape
                objectName: "printOrientation"
                model: [qsTr("Portrait"), qsTr("Landscape")]
                onActivated: function(index) { if (dialog.job !== null) dialog.job.landscape = index === 1 }
            }
        }
    }
}
