// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// A single-record form: QindaTK's FormDesigner over the form's saved
// design (a toolkit design document, opened with its own history) and a
// presentation mirror of the table (base_snapshot.h), with record
// navigation — first / previous / n of N / next / last / new / delete.
// Saves go through BaseService (typed, validated, one undo step each); the
// designer shows the engine's refusals as its validation messages. File
// fields (M65) are not in the designer's mirror: each shows as a
// FileFieldPanel under the form, and the File commands act on the one
// last used (the first by default).
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string formName: ""
    property string table: ""
    property string title: ""
    property var rowIds: []
    property int position: -1        // index into rowIds; -1 = a new record
    property var session: null
    property string savedRevision: ""
    property string notice: ""
    readonly property var designDocument: session !== null ? session.model : null
    readonly property bool designDirty: designDocument !== null && designDocument.revision !== savedRevision
    readonly property bool designing: designer.mode === "design"
    property var fileFields: []      // the table's file fields [{name, caption}]
    property string fileField: ""    // the one the File commands act on
    property var fileHas: ({})       // file field → holds a file (the panels report it)
    readonly property var fileTarget: fileField.length === 0 || position < 0 ? ({})
        : { table: table, rowid: Number(rowIds[position]), field: fileField, hasFile: fileHas[fileField] === true }
    onFileTargetChanged: if (window !== null && visible) window.baseHost.fileTarget = fileTarget

    function open(name, mode) {
        const r = base.openForm(name)
        if (!r.ok) { window.notify(qsTr("Cannot open %1").arg(name), r.error, "danger"); return false }
        closeSession()
        mirror.open(r.mirrorPath)
        session = manager.openDocument(r.designPath)
        if (session === null) { window.notify(qsTr("Cannot open %1").arg(name), manager.lastError, "danger"); return false }
        savedRevision = designDocument.revision
        formName = name
        table = r.table
        title = r.title
        notice = r.notice || ""
        fileFields = base.tableFields(table).filter(function(f) { return f.type === "file" })
        fileField = fileFields.length > 0 ? fileFields[0].name : ""
        designer.mode = mode === "design" ? "design" : "edit"
        reloadRows(0)
        syncHost()
        return true
    }
    function closeSession() {
        if (session !== null) manager.closeDocument(session.documentId, Tk.DocumentManager.Discard)
        session = null
    }
    function syncHost() {
        if (window === null || window.view !== "form") return
        window.baseHost.designDirty = designDirty
        window.baseHost.designHistory = designing
        window.baseHost.designCanUndo = designDocument !== null && designDocument.canUndo
        window.baseHost.designCanRedo = designDocument !== null && designDocument.canRedo
    }
    onDesignDirtyChanged: syncHost()
    onDesigningChanged: syncHost()
    onVisibleChanged: if (visible) { syncHost(); if (table.length) reloadRows(position); window.baseHost.fileTarget = fileTarget }

    function reloadRows(at) {
        rowIds = base.formRowIds(table)
        goTo(rowIds.length === 0 ? -1 : Math.max(0, Math.min(at, rowIds.length - 1)))
    }
    function goTo(index) {
        position = index
        const r = base.formRecord(table, index >= 0 ? Number(rowIds[index]) : 0)
        designer.recordId = r.recordId || ""
        designer.record = r.record || ({})
    }
    function newRecord() { if (!designing) goTo(-1) }
    function refresh() { reloadRows(position) }
    function saveRecord() { if (designer.mode === "edit") designer.requestRecordSave() }
    function deleteRecord() {
        if (position < 0) { goTo(rowIds.length ? 0 : -1); return }
        window.confirmDialog.ask(qsTr("Delete this record?"), qsTr("Undo brings it back."), "danger", qsTr("Delete"), function() {
            const r = base.deleteFormRecord(table, Number(rowIds[position]))
            if (!r.ok) window.notify(qsTr("Not deleted"), r.error, "danger")
            else reloadRows(position)
        })
    }
    function togglePreview() { designer.mode = designer.mode === "preview" ? "edit" : "preview" }
    function save() {
        if (designDocument === null || !designDirty) return true
        const r = base.saveFormDesign(formName, designDocument.design)
        if (!r.ok) { window.notify(qsTr("Form not saved"), r.error, "danger"); return false }
        savedRevision = designDocument.revision
        return true
    }
    function discard() { if (formName.length) open(formName, designer.mode) }
    function undo() { if (designDocument !== null) designDocument.undo() }
    function redo() { if (designDocument !== null) designDocument.redo() }

    Tk.DocumentManager { id: manager }
    Tk.LocalDatabaseProvider { id: mirror }
    Connections {
        target: view.designDocument
        function onHistoryChanged() { view.syncHost() }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "formHeader"
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.sm
            paddingLeft: Tk.Theme.space.md
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                Tk.Icon { name: "layout-template"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                Tk.Heading { objectName: "formName"; level: 3; text: view.formName; Tk.Flex.shrink: 0 }
                Tk.Caption { text: qsTr("records of %1").arg(view.table); Tk.Flex.shrink: 0 }
                Tk.Spacer { }
                Tk.Badge {
                    visible: view.designDirty
                    variant: "warning"
                    text: qsTr("layout not saved")
                }
            }
        }
        Tk.Notice { visible: view.notice.length > 0; variant: "warning"; text: view.notice; dismissible: true; onDismissed: view.notice = "" }
        Tk.Scroll {
            id: body
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            accessibleName: view.formName
            Tk.Flex {
                width: body.width
                direction: Tk.Flex.Column
                align: Tk.Flex.Center
                padding: Tk.Theme.space.lg
                Tk.Card {
                    width: Math.min(body.width - Tk.Theme.space.lg * 2, Tk.Theme.size.fieldWidth * 4)
                    padding: Tk.Theme.space.lg
                    Tk.FormDesigner {
                        id: designer
                        objectName: "baseFormDesigner"
                        provider: mirror
                        designDocument: view.designDocument
                        tableName: view.table
                        tooltip: view.title
                        onRecordSaveRequested: function(requestId, tableName, recordId, changes) {
                            const r = view.base.saveFormRecord(tableName, Number(recordId) || 0, changes)
                            designer.acknowledge(requestId, r.ok ? "accepted" : "rejected", r.ok ? qsTr("Saved") : r.error)
                            if (r.ok) {
                                view.rowIds = view.base.formRowIds(view.table)
                                view.goTo(Math.max(0, view.rowIds.map(Number).indexOf(Number(r.rowid))))
                            }
                        }
                        onDesignSaveRequested: function(requestId, design) {
                            const ok = view.save()
                            designer.acknowledge(requestId, ok ? "accepted" : "rejected", ok ? qsTr("Layout saved") : qsTr("Not saved"))
                        }
                    }
                }
                Tk.Flex {
                    objectName: "formFiles"
                    visible: view.fileFields.length > 0 && !view.designing
                    width: Math.min(body.width - Tk.Theme.space.lg * 2, Tk.Theme.size.fieldWidth * 4)
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.sm
                    Repeater {
                        model: view.fileFields
                        delegate: FileFieldPanel {
                            id: fileFieldItem
                            required property var modelData
                            window: view.window
                            table: view.table
                            rowid: view.position >= 0 ? Number(view.rowIds[view.position]) : 0
                            field: modelData.name
                            label: modelData.caption || modelData.name
                            pictureHeight: Tk.Theme.size.thumbnailHeight * 1.5
                            Tk.Flex.basis: Tk.Theme.size.panelMinWidth
                            Tk.Flex.shrink: 0
                            onHasFileChanged: {
                                const next = Object.assign({}, view.fileHas)
                                next[field] = hasFile
                                view.fileHas = next
                            }
                            TapHandler { onTapped: view.fileField = fileFieldItem.field }
                        }
                    }
                }
            }
        }
        Tk.Box {
            objectName: "formNavigator"
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            borderTop: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Caption { text: qsTr("Record"); Tk.Flex.shrink: 0 }
                Tk.IconButton { objectName: "formFirst"; iconName: "chevrons-left"; small: true; tooltip: qsTr("First record"); enabled: view.rowIds.length > 0 && !view.designing; onClicked: view.goTo(0) }
                Tk.IconButton { objectName: "formPrevious"; iconName: "chevron-left"; small: true; tooltip: qsTr("Previous record"); enabled: view.position > 0 && !view.designing; onClicked: view.goTo(view.position - 1) }
                Tk.Label {
                    objectName: "formPosition"
                    mono: true
                    text: view.position < 0 ? qsTr("New of %1").arg(view.rowIds.length) : qsTr("%1 of %2").arg(view.position + 1).arg(view.rowIds.length)
                    Tk.Flex.shrink: 0
                }
                Tk.IconButton { objectName: "formNext"; iconName: "chevron-right"; small: true; tooltip: qsTr("Next record"); enabled: view.position >= 0 && view.position + 1 < view.rowIds.length && !view.designing; onClicked: view.goTo(view.position + 1) }
                Tk.IconButton { objectName: "formLast"; iconName: "chevrons-right"; small: true; tooltip: qsTr("Last record"); enabled: view.rowIds.length > 0 && !view.designing; onClicked: view.goTo(view.rowIds.length - 1) }
                Tk.IconButton { objectName: "formNew"; iconName: "list-plus"; small: true; tooltip: qsTr("New record"); enabled: !view.designing; onClicked: view.newRecord() }
                Tk.IconButton { objectName: "formDelete"; iconName: "list-minus"; small: true; tooltip: qsTr("Delete record"); enabled: view.position >= 0 && !view.designing; onClicked: view.deleteRecord() }
                Tk.Spacer { }
                Tk.Caption {
                    text: view.designing ? qsTr("Design: labels, order and which fields show")
                                         : qsTr("Changes are saved with Save record, each one undoable")
                    Tk.Flex.shrink: 1
                    elide: Text.ElideRight
                }
            }
        }
    }
}
