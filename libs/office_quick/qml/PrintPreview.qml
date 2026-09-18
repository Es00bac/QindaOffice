// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// File ▸ Print Preview… without QPrintPreviewDialog: the job's pages,
// rasterised from the very PDF it would print, in a scrolling column with
// a zoom island. "Print…" hands the job to the PrintDialog.
Tk.Dialog {
    id: dialog

    property PrintJob job: null
    property real zoom: 0.6

    signal printRequested(PrintJob job)

    title: qsTr("Print preview")
    subtitle: job !== null && job.pageCount > 0 ? qsTr("%n page(s)", "", job.pageCount) : ""
    primaryText: qsTr("Print…")
    secondaryText: qsTr("Close")
    dialogWidth: 760
    primaryEnabled: job !== null

    onOpened: {
        if (job !== null && !job.preparePreview()) {
            notice.visible = true
        } else {
            notice.visible = false
        }
    }
    onAccepted: if (job !== null) dialog.printRequested(job)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Notice {
            id: notice
            visible: false
            variant: "warning"
            text: job !== null && job.lastError.length > 0 ? job.lastError
                : !PrintJob.previewAvailable ? qsTr("Preview rendering is not available in this build.")
                : ""
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ZoomControl {
                value: dialog.zoom
                from: 0.25
                to: 3
                presets: [0.25, 0.5, 0.6, 0.75, 1, 1.5, 2, 3]
                onValueModified: function(v) { dialog.zoom = v }
            }
            Tk.Spacer { }
            Tk.Caption { text: job !== null ? qsTr("%1 pages").arg(job.pageCount) : "" }
        }
        Item {
            implicitWidth: 720
            implicitHeight: 480
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; radius: Tk.Theme.radius.sm }
            Tk.Scroll {
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.xs
                overflowX: Tk.Scroll.Auto
                Tk.Flex {
                    direction: Tk.Flex.Column
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.md
                    Repeater {
                        model: dialog.job !== null ? dialog.job.pageCount : 0
                        PagePreviewItem {
                            required property int index
                            objectName: "previewPage_" + index
                            job: dialog.job
                            pageIndex: index
                            zoom: dialog.zoom
                        }
                    }
                }
            }
        }
    }
}
