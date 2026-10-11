// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Show

// The M91 (D-099) dialogs of the QindaShow window, and the host's requests
// that open them: Insert ▸ Action and Link, Custom Shows (and Define Custom
// Show), Set Up Slide Show, the rehearsal's "keep the new timings?",
// Create a Video (with its progress) and Export Slides as Pictures, and a
// medium's Trim, Fade and Bookmarks.
//
// AGENT-NOTE: a plain QtObject the window holds in ONE property, so the
// window keeps its single content child (AGENTS.md QML rules); each popup
// is placed on `host`'s overlay through Tk.Dialog's hostItem.
QtObject {
    id: dialogs
    required property ShowHost showHost
    required property Item host

    readonly property ActionSettingsDialog actionDialog: ActionSettingsDialog { hostItem: dialogs.host; showHost: dialogs.showHost }
    readonly property LinkDialog linkDialog: LinkDialog { hostItem: dialogs.host; showHost: dialogs.showHost }
    readonly property CustomShowsDialog customShowsDialog: CustomShowsDialog {
        hostItem: dialogs.host
        showHost: dialogs.showHost
        onDefineRequested: function(index) { dialogs.defineDialog.edit(index) }
    }
    readonly property DefineShowDialog defineDialog: DefineShowDialog {
        hostItem: dialogs.host
        showHost: dialogs.showHost
        onClosed: if (dialogs.customShowsDialog.reopen) dialogs.customShowsDialog.open()
    }
    readonly property SetUpShowDialog setUpDialog: SetUpShowDialog { hostItem: dialogs.host; showHost: dialogs.showHost }
    readonly property MediaEditDialog mediaDialog: MediaEditDialog { hostItem: dialogs.host; showHost: dialogs.showHost }
    readonly property VideoExportDialog videoDialog: VideoExportDialog {
        hostItem: dialogs.host
        showHost: dialogs.showHost
        onFileWanted: videoFile.open()
    }
    readonly property ExportPicturesDialog picturesDialog: ExportPicturesDialog {
        hostItem: dialogs.host
        showHost: dialogs.showHost
        onFileWanted: picturesFile.open()
    }
    readonly property Tk.MessageDialog keepTimings: Tk.MessageDialog {
        objectName: "keepTimingsDialog"
        hostItem: dialogs.host
        property string total: ""
        title: qsTr("Rehearse Timings")
        variant: "question"
        text: qsTr("The total time for the slide show was %1. Do you want to keep the new slide timings?").arg(total)
        primaryText: qsTr("Keep")
        secondaryText: qsTr("Discard")
        onAccepted: dialogs.showHost.slideShow.keepRehearsal(true)
        onRejected: dialogs.showHost.slideShow.keepRehearsal(false)
    }
    readonly property Dialogs.FileDialog videoFile: Dialogs.FileDialog {
        title: qsTr("Create a Video")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MPEG-4 Video (*.mp4)")]
        selectedFile: dialogs.showHost.exporter.suggestedFile("mp4")
        onAccepted: dialogs.videoDialog.start(selectedFile)
    }
    readonly property Dialogs.FileDialog picturesFile: Dialogs.FileDialog {
        title: qsTr("Export Slides as Pictures")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: dialogs.picturesDialog.jpeg ? [qsTr("JPEG Image (*.jpg)")] : [qsTr("PNG Image (*.png)")]
        selectedFile: dialogs.showHost.exporter.suggestedFile(dialogs.picturesDialog.jpeg ? "jpg" : "png")
        onAccepted: dialogs.picturesDialog.write(selectedFile)
    }

    readonly property list<QtObject> requests: [
        Connections {
            target: dialogs.showHost.actionSettings
            function onActionDialogRequested() { dialogs.actionDialog.open() }
            function onLinkDialogRequested() { dialogs.linkDialog.open() }
        },
        Connections {
            target: dialogs.showHost.slideShow
            function onCustomShowsRequested() { dialogs.customShowsDialog.open() }
            function onSetUpRequested() { dialogs.setUpDialog.open() }
            function onKeepTimingsRequested(total) { dialogs.keepTimings.total = total; dialogs.keepTimings.open() }
        },
        Connections {
            target: dialogs.showHost.exporter
            function onVideoDialogRequested() { dialogs.videoDialog.open() }
            function onPicturesDialogRequested() { dialogs.picturesDialog.open() }
        },
        Connections {
            target: dialogs.showHost.media
            function onEditRequested() { dialogs.mediaDialog.open() }
        }
    ]
}
