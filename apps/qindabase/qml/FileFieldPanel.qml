// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One file field of one record (M65, D-074): its picture — a Tk.Thumbnail
// of the preview BaseService::fileInfo writes — or its kind and size, and
// Insert / Save / Remove aimed at this field (the Records ▸ File commands
// run the same BaseCommands functions on the host's fileTarget). The
// datasheet shows one beside the grid for its current cell; a form shows
// one per file field under the record.
//
// AGENT-NOTE (toolkit gap): Tk.EditableGrid cells draw text and an icon,
// not a picture, so the datasheet's thumbnail lives here, not in the cell.
Tk.Box {
    id: panel

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string table: ""
    property var rowid: 0
    property string field: ""
    property string label: field
    property bool editable: true
    property real pictureHeight: Tk.Theme.size.thumbnailHeight * 2
    property var info: ({})
    readonly property bool ready: base !== null && table.length > 0 && field.length > 0 && Number(rowid) > 0
    readonly property bool hasFile: info.hasFile === true

    // AGENT-GUARD: reads the inputs, not `ready` — a change handler can run
    // before the binding it shares a signal with — and runs once after the
    // inputs settle (Qt.callLater coalesces): fileInfo reads the whole file.
    function reload() {
        const usable = base !== null && table.length > 0 && field.length > 0 && Number(rowid) > 0
        info = usable ? base.fileInfo(table, Number(rowid), field) : ({})
    }
    function scheduleReload() { Qt.callLater(panel.reload) }
    function target() { return { table: table, rowid: Number(rowid), field: field, hasFile: hasFile } }

    onTableChanged: scheduleReload()
    onRowidChanged: scheduleReload()
    onFieldChanged: scheduleReload()
    onBaseChanged: scheduleReload()
    Connections {
        target: panel.base
        function onRecordsChanged() { panel.scheduleReload() }
    }

    objectName: "fileFieldPanel_" + field
    color: Tk.Theme.color.surface
    padding: Tk.Theme.space.sm
    borderWidth: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider
    radius: Tk.Theme.radius.sm

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Overline { title: panel.label }
        Tk.Thumbnail {
            objectName: "fileFieldThumbnail"
            Tk.Flex.basis: panel.pictureHeight
            Tk.Flex.shrink: 0
            source: panel.info.previewUrl || ""
            placeholderIcon: panel.hasFile ? "file" : "paperclip"
            crop: false
            caption: panel.hasFile ? panel.info.sizeText : ""
            tooltip: panel.hasFile ? panel.info.label : qsTr("No file")
        }
        Tk.Caption {
            objectName: "fileFieldKind"
            elide: Text.ElideRight
            text: panel.hasFile ? panel.info.kind
                                : panel.ready ? qsTr("No file yet") : qsTr("Save the record to add a file")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.IconButton {
                objectName: "fileFieldInsert"
                iconName: "paperclip"
                small: true
                tooltip: qsTr("Insert a file…")
                enabled: panel.ready && panel.editable
                onClicked: panel.window.commands.insertFile(panel.target())
            }
            Tk.IconButton {
                objectName: "fileFieldSave"
                iconName: "download"
                small: true
                tooltip: qsTr("Save the file as…")
                enabled: panel.hasFile
                onClicked: panel.window.commands.saveFile(panel.target())
            }
            Tk.IconButton {
                objectName: "fileFieldRemove"
                iconName: "file-minus"
                small: true
                tooltip: qsTr("Remove the file")
                enabled: panel.hasFile && panel.editable
                onClicked: panel.window.commands.removeFile(panel.target())
            }
        }
    }
}
