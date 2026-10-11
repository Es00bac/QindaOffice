// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Dialog {
    id: dialog
    required property var note
    property var entries: []
    property string selectedId: ""
    property string preview: ""
    property string notice: ""
    property bool keeping: true
    property bool saved: false
    objectName: "noteVersionsDialog"
    title: qsTr("Page Versions")
    subtitle: note.currentPageTitle
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: qsTr("Close")
    secondaryText: ""

    function reload() {
        const r = note.pageVersions()
        entries = r.entries || []
        keeping = r.enabled === true
        saved = r.entries !== undefined
        notice = r.error || ""
        let at = entries.findIndex(function(e) { return e.id === selectedId })
        if (at < 0) at = entries.length > 0 ? 0 : -1
        versions.currentIndex = at
        select(at)
    }
    function select(index) {
        selectedId = index >= 0 && index < entries.length ? entries[index].id : ""
        preview = ""
        if (selectedId.length) {
            const r = note.pageVersionPreview(selectedId)
            if (r.ok) preview = r.image
            else notice = r.error
        }
    }
    function complete(r) {
        if (!r.ok) { notice = r.error; return }
        reload()
    }
    onOpened: reload()
    Connections {
        target: dialog.note
        function onCurrentPageChanged() { if (dialog.visible) dialog.close() }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Caption {
            text: qsTr("Earlier saved content is kept every five minutes: up to 32 versions and 256 MiB per page. Restore is one undo step and keeps the current page as a version.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.CheckBox {
            objectName: "noteVersionsKeep"
            text: qsTr("Keep automatic versions for this notebook")
            checked: dialog.keeping
            enabled: dialog.saved
            onClicked: dialog.complete(dialog.note.keepPageVersions(checked))
        }
        Tk.Notice {
            objectName: "noteVersionsNotice"
            visible: dialog.notice.length > 0
            variant: "warning"
            text: dialog.notice
        }
        Tk.ComboBox {
            id: versions
            objectName: "noteVersionList"
            model: dialog.entries.map(function(e) { return e.captured + " — " + e.title })
            enabled: dialog.entries.length > 0
            tooltip: qsTr("Retained versions, newest first")
            onActivated: function(index) { dialog.select(index) }
        }
        Tk.Caption {
            visible: dialog.entries.length === 0
            text: qsTr("No versions yet. Save a version now, or edit a saved page.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.Caption { visible: dialog.preview.length > 0; text: qsTr("First page preview") }
        Image {
            objectName: "noteVersionPreview"
            visible: dialog.preview.length > 0
            source: dialog.preview
            fillMode: Image.PreserveAspectFit
            asynchronous: false
            Tk.Flex.basis: Tk.Theme.size.control * 8
            Tk.Flex.shrink: 0
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "noteVersionSave"
                text: qsTr("Save Version Now")
                iconName: "save"
                enabled: dialog.saved
                onClicked: dialog.complete(dialog.note.savePageVersion())
            }
            Tk.Button {
                objectName: "noteVersionRestore"
                text: qsTr("Restore Version")
                iconName: "history"
                enabled: dialog.selectedId.length > 0
                onClicked: {
                    const r = dialog.note.restorePageVersion(dialog.selectedId)
                    if (r.ok) dialog.close()
                    else dialog.notice = r.error
                }
            }
            Tk.Button {
                objectName: "noteVersionDelete"
                text: qsTr("Delete Version…")
                iconName: "trash-2"
                enabled: dialog.selectedId.length > 0
                onClicked: deleteVersion.open()
            }
        }
    }
    Tk.MessageDialog {
        id: deleteVersion
        objectName: "noteVersionDeleteConfirm"
        title: qsTr("Delete page version?")
        text: qsTr("This removes the selected saved version. The current page stays as it is. This cannot be undone.")
        primaryText: qsTr("Delete")
        destructive: true
        variant: "danger"
        onAccepted: dialog.complete(dialog.note.deletePageVersion(dialog.selectedId))
    }
}
