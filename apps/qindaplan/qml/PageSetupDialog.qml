// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Gantt page setup: the date range to print (empty = the whole project)
// and how many pages wide the timescale is fitted to. Print, Print Preview
// and Export PDF all use it; the page count is the painter's own.
Tk.Dialog {
    id: dialog

    property PlanHost host: null

    objectName: "pageSetupDialog"
    title: qsTr("Gantt Page Setup")
    primaryText: qsTr("Preview")
    secondaryText: qsTr("Close")

    function openDialog() {
        fromField.text = host.printFrom
        toField.text = host.printTo
        pagesWide.value = host.printPagesWide
        open()
    }
    function apply() {
        host.printFrom = fromField.text.trim()
        host.printTo = toField.text.trim()
        host.printPagesWide = pagesWide.value
    }
    onAccepted: {
        apply()
        host.actions.trigger("filePrintPreviewAction")
    }
    onRejected: apply()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("From")
            hint: qsTr("yyyy-mm-dd; empty = the project start")
            Tk.TextField { id: fromField; objectName: "pageSetupFrom"; mono: true; onEditingFinished: dialog.host.printFrom = text.trim() }
        }
        Tk.PropertyRow {
            label: qsTr("To")
            hint: qsTr("yyyy-mm-dd; empty = the project finish")
            Tk.TextField { id: toField; objectName: "pageSetupTo"; mono: true; onEditingFinished: dialog.host.printTo = text.trim() }
        }
        Tk.PropertyRow {
            label: qsTr("Pages wide")
            Tk.NumberField {
                id: pagesWide
                objectName: "pageSetupPagesWide"
                from: 1
                to: 20
                small: true
                onValueModified: function(value) { dialog.host.printPagesWide = value }
            }
        }
        Tk.Caption {
            objectName: "pageSetupSummary"
            // Re-read whenever the range or the width changes.
            readonly property int pages: dialog.host === null ? 0
                : (dialog.host.printFrom + dialog.host.printTo + dialog.host.printPagesWide).length >= 0 ? dialog.host.ganttPageCount() : 0
            text: qsTr("%n landscape page(s) on the default paper.", "", pages)
        }
    }
}
