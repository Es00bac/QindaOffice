// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// File ▸ Import ▸ Outlook Data File (M76, D-085): the progress of the
// running import (MailService.outlookImport, a PstImporter) and, when it
// ends, its summary — folders, messages, contacts and groups, events,
// tasks and the items it skipped with the first reasons. The dialog follows
// the importer, so an import started from the file manager shows here too;
// Hide leaves it running (the toast reports the end), Cancel stops it after
// the current slice and keeps what was imported.
Tk.Dialog {
    id: dialog

    property MailService service: null
    readonly property PstImporter importer: service !== null ? service.outlookImport : null
    readonly property bool running: importer !== null && importer.running
    readonly property var counts: importer !== null ? importer.counts : ({})
    readonly property var summary: importer !== null ? importer.summary : ({})
    readonly property string state_: String(summary.state || "")
    signal showFolderRequested(string folderKey)

    objectName: "pstImportDialog"
    title: running ? qsTr("Importing an Outlook data file")
                   : state_ === "rejected" ? qsTr("The Outlook data file was not imported")
                   : state_ === "cancelled" ? qsTr("Outlook import cancelled") : qsTr("Outlook data file imported")
    subtitle: importer !== null ? importer.fileName : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.2
    primaryText: running ? qsTr("Cancel import") : qsTr("Show imported mail")
    primaryEnabled: running || Number(summary.messages || 0) > 0
    secondaryText: running ? qsTr("Hide") : qsTr("Close")
    destructive: running

    function countText(key) { return String(Number(dialog.counts[key] || 0)) }

    onAccepted: {
        if (dialog.running) {
            dialog.importer.cancel()
            Qt.callLater(dialog.open)   // stays up to show what was imported
            return
        }
        dialog.showFolderRequested(String(dialog.summary.folderKey || ""))
    }
    Connections {
        target: dialog.importer
        function onStateChanged() { if (dialog.importer.running && !dialog.visible) dialog.open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            objectName: "pstImportRunning"
            visible: dialog.running
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Label {
                objectName: "pstImportPhase"
                text: dialog.importer === null ? "" : dialog.importer.phase === "scanning"
                    ? qsTr("Reading the folders of the file…")
                    : qsTr("Importing item %1 of %2").arg(dialog.importer.processed).arg(dialog.importer.total)
            }
            Tk.ProgressBar {
                objectName: "pstImportProgress"
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                from: 0
                to: 1
                value: dialog.importer !== null ? dialog.importer.progress : 0
                indeterminate: dialog.importer !== null && dialog.importer.phase === "scanning"
                tooltip: qsTr("Import progress")
            }
            Tk.Caption {
                objectName: "pstImportFolder"
                visible: dialog.importer !== null && dialog.importer.currentFolder.length > 0
                text: qsTr("Folder: %1").arg(dialog.importer !== null ? dialog.importer.currentFolder : "")
                elide: Text.ElideMiddle
            }
        }

        Tk.Notice {
            objectName: "pstImportResult"
            visible: !dialog.running && dialog.state_.length > 0
            variant: dialog.state_ === "rejected" ? "danger"
                   : dialog.state_ === "cancelled" || Number(dialog.summary.skipped || 0) > 0 ? "warning" : "success"
            title: dialog.state_ === "rejected" ? qsTr("Nothing was imported")
                 : dialog.state_ === "cancelled" ? qsTr("Stopped after %1 of %2 items").arg(Number(dialog.importer ? dialog.importer.processed : 0)).arg(Number(dialog.summary.total || 0))
                 : qsTr("Everything is in QindaMail")
            text: dialog.state_ === "rejected" ? String(dialog.summary.error || "")
                : qsTr("Mail is in the folder “%1” under On this computer; contacts and groups are in People, "
                       + "appointments in your calendar, tasks on the task board. Importing the same file again "
                       + "updates them instead of adding copies.").arg(String(dialog.summary.folder || ""))
        }

        Tk.Flex {
            objectName: "pstImportCounts"
            direction: Tk.Flex.Column
            gap: 0
            Tk.KeyValue { objectName: "pstCountFolders"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Folders"); value: dialog.countText("folders") }
            Tk.KeyValue { objectName: "pstCountMessages"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Messages"); value: dialog.countText("messages") }
            Tk.KeyValue { objectName: "pstCountContacts"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Contacts"); value: dialog.countText("contacts") }
            Tk.KeyValue { objectName: "pstCountGroups"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Contact groups"); value: dialog.countText("groups") }
            Tk.KeyValue { objectName: "pstCountEvents"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Calendar items"); value: dialog.countText("events") }
            Tk.KeyValue { objectName: "pstCountTasks"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Tasks"); value: dialog.countText("tasks") }
            Tk.KeyValue { objectName: "pstCountSkipped"; Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Skipped"); value: dialog.countText("skipped"); accent: Number(dialog.counts.skipped || 0) > 0 }
        }

        Tk.Caption {
            objectName: "pstImportWarnings"
            visible: !dialog.running && (dialog.summary.warnings || []).length > 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            wrapMode: Text.Wrap
            text: (dialog.summary.warnings || []).slice(0, 5).join("\n")
        }
    }
}
