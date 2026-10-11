// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Export ▸ Create a Video (M91, D-099), as PowerPoint's: the resolution
// (720p HD, 1080p Full HD, 4K Ultra HD), the seconds spent on each slide
// without a timing, and whether the rehearsed timings are used. Create
// Video asks for the file, then the dialog shows the progress of the
// export running in the background (ShowExport, show_model's
// video_export.h, the system's ffmpeg) with Cancel; a cancelled export
// leaves no file. Without ffmpeg the dialog says so and cannot start.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowExport exporter: showHost.exporter
    readonly property var resolutions: ["720p", "1080p", "4k"]
    property string message: ""
    signal fileWanted()

    // The save dialog's answer: start, and show the progress.
    function start(file) {
        dialog.message = ""
        if (dialog.exporter.startVideo(dialog.resolutions[resolution.currentIndex], Math.round(seconds.value), timings.checked, file)) {
            dialog.open()
        }
    }

    objectName: "videoExportDialog"
    title: qsTr("Create a Video")
    primaryText: dialog.exporter.exporting ? qsTr("Hide") : qsTr("Create Video")
    primaryEnabled: dialog.exporter.videoAvailable && dialog.showHost.slideCount > 0
    secondaryText: dialog.exporter.exporting ? qsTr("Cancel Export") : qsTr("Cancel")
    dialogWidth: Tk.Theme.size.dialogWidth
    onAccepted: if (!dialog.exporter.exporting) dialog.fileWanted()
    onRejected: if (dialog.exporter.exporting) dialog.exporter.cancelVideo()

    readonly property Connections watcher: Connections {
        target: dialog.exporter
        function onVideoFinished(ok, message) { dialog.message = message }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice {
            objectName: "videoUnavailable"
            visible: !dialog.exporter.videoAvailable
            variant: "warning"
            text: dialog.exporter.videoUnavailableReason
        }
        Tk.PropertyRow {
            label: qsTr("Resolution")
            Tk.ComboBox {
                id: resolution
                objectName: "videoResolution"
                enabled: !dialog.exporter.exporting
                width: Tk.Theme.size.dialogWidth * 0.39
                model: [qsTr("HD (720p)"), qsTr("Full HD (1080p)"), qsTr("Ultra HD (4K)")]
                currentIndex: 1
            }
        }
        Tk.PropertyRow {
            label: qsTr("Seconds spent on each slide")
            Tk.NumberField {
                id: seconds
                objectName: "videoSeconds"
                enabled: !dialog.exporter.exporting
                from: 1
                to: 600
                value: 5
                decimals: 0
                scrub: false
                suffix: qsTr(" s")
                onValueModified: function(next) { value = next }
            }
        }
        Tk.CheckBox {
            id: timings
            objectName: "videoUseTimings"
            enabled: !dialog.exporter.exporting
            checked: true
            text: qsTr("Use recorded timings")
        }
        Tk.Caption {
            width: Tk.Theme.size.dialogWidth * 0.75
            wrapMode: Text.Wrap
            text: qsTr("Slides play in order without the hidden ones; builds play spread over each slide; transitions are cross-fades; slide sounds are mixed in.")
        }
        Tk.ProgressBar {
            objectName: "videoProgress"
            visible: dialog.exporter.exporting
            implicitWidth: Tk.Theme.size.dialogWidth * 0.75
            value: dialog.exporter.progress
        }
        Tk.Label {
            objectName: "videoMessage"
            visible: dialog.message.length > 0 || dialog.exporter.exporting
            width: Tk.Theme.size.dialogWidth * 0.75
            wrapMode: Text.Wrap
            text: dialog.exporter.exporting ? qsTr("Creating the video… %1%").arg(Math.round(dialog.exporter.progress * 100)) : dialog.message
        }
    }
}
