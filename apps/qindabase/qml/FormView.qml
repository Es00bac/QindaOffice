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
//
// M89 (D-097): the form is laid out with the designer's positioned
// controls — labels, text boxes (bound, or calculated through
// `expressionEvaluator`), combo and list boxes (a lookup's items are the
// related records, FormDesignBridge.qml), check boxes, subforms by form
// name (FormSubform.qml), command buttons running a macro action
// (runCommand), tab controls, images, lines and rectangles. The designer
// is on its host-owned path; FormDesignBridge turns each finished gesture
// into one design-document command. A macro's ApplyFilter narrows the
// records the navigator walks.
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
    property bool opening: false
    readonly property var designDocument: session !== null ? session.model : null
    readonly property bool designDirty: designDocument !== null && designDocument.revision !== savedRevision
    readonly property bool designing: designer.mode === "design"
    readonly property bool recordEditing: designer.mode === "edit"
    property alias formDesigner: designer
    property var fileFields: []      // the table's file fields [{name, caption}]
    property string fileField: ""    // the one the File commands act on
    property var fileHas: ({})       // file field → holds a file (the panels report it)
    property var stored: ({})        // the record's storage values (subform links)
    property string filter: ""       // M80: ApplyFilter's where condition
    readonly property FormDesignBridge bridge: FormDesignBridge { view: view }
    readonly property FormEventBridge events: FormEventBridge { view: view }
    readonly property var subforms: (bridge.presented.controls || []).filter(function(c) { return c.type === "subform" })
    // The width the laid-out form needs (design units are logical pixels):
    // its controls' right edge, the canvas margin and the card's padding,
    // and in Design the property sheet beside it. A narrower window
    // scrolls sideways, as Access does, instead of cutting controls off.
    readonly property real formWidth: {
        let right = 0
        for (const c of (bridge.presented.controls || [])) right = Math.max(right, (Number(c.x) || 0) + (Number(c.width) || 0))
        return right + Tk.Theme.space.xl + Tk.Theme.space.lg * 2
               + (designing ? Tk.Theme.size.panelMinWidth + Tk.Theme.space.md : 0)
    }
    // AGENT-GUARD: use the allocated view width, not the Scroll viewport,
    // whose size depends on this card's overflow and would form a loop.
    readonly property real cardWidth: Math.max(formWidth, Math.min(view.width - Tk.Theme.space.lg * 2,
                                                                   Tk.Theme.size.fieldWidth * (designing ? 6 : 4)))
    readonly property var fileTarget: fileField.length === 0 || position < 0 ? ({})
        : { table: table, rowid: Number(rowIds[position]), field: fileField, hasFile: fileHas[fileField] === true }
    onFileTargetChanged: if (window !== null && visible) window.baseHost.fileTarget = fileTarget

    function open(name, mode) {
        const r = base.openForm(name)
        if (!r.ok) { window.notify(qsTr("Cannot open %1").arg(name), r.error, "danger"); return false }
        closeSession()
        opening = true
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
        designer.selectControl("")
        designer.mode = mode === "design" ? "design" : "edit"
        filter = ""
        bridge.refreshLookups()
        reloadRows(0)
        syncHost()
        return true
    }
    function finishOpen(done, failed) {
        opening = false
        events.runMany([{ event: "open", control: "" }, { event: "current", control: "" }], done, function(error) {
            window.commands.show("start", "", "")
            if (failed) failed(error)
            else events.problem(error)
        })
    }
    // The designer's API, for Add Subform and Add Button (one undo step).
    function addControl(type, properties) {
        if (designDocument === null) return ""
        return designer.addControl(type, properties)
    }
    function subformIndex(controlId) {
        for (let i = 0; i < subforms.length; ++i) if (String(subforms[i].id) === String(controlId)) return i < 4 ? i : -1
        return -1
    }
    // A command button (QindaTK's controlCommandRequested): its action id is
    // one of the macro actions run with the button's arguments, "runMacro"
    // (arguments.macro), or a saved macro's name; M80's embedded macros of
    // several steps run their `actions`.
    function runCommand(controlId, actionId) {
        if (!recordEditing) return
        if (events.name("click", controlId).length) { events.run("click", controlId); return }
        const control = bridge.controlById(designDocument !== null ? (designDocument.form.controls || []) : [], controlId) || ({})
        const runner = window.commands.macroRunner
        const caption = String(control.caption || control.name || qsTr("The button"))
        const args = control.arguments || ({})
        if (Array.isArray(control.actions) && control.actions.length > 0) runner.run(control.actions, caption)
        else if (actionId === "runMacro") runner.runNamed(String(args.macro || ""))
        else if (actionId.length === 0)
            window.notify(qsTr("%1 does nothing yet").arg(caption), qsTr("Choose what it does: Design ▸ Properties ▸ Action."), "info")
        else if (base.macroActions.some(function(a) { return a.id === actionId })) runner.run([{ action: actionId, arguments: args }], caption)
        else if (base.macroNames.indexOf(actionId) >= 0) runner.runNamed(actionId)
        else window.notify(qsTr("%1 did not run").arg(caption), qsTr("QindaBase has no action or macro named %1.").arg(actionId), "warning")
    }
    function evaluate(expression, record) {
        const r = base.evaluateFormExpression(table, expression, bridge.toStored(record))
        return r.ok ? r.text : "#Error"
    }
    // A macro's ApplyFilter / ShowAllRecords: {ok, error}.
    function applyFilter(where) {
        if (where.trim().length === 0) { filter = ""; reloadRows(0); return { ok: true } }
        const r = base.whereRowIds(table, where)
        if (!r.ok) return r
        filter = where
        rowIds = r.rowIds
        goTo(rowIds.length > 0 ? 0 : -1)
        return { ok: true }
    }
    function goToRecord(which) {
        if (designing) return
        const last = rowIds.length - 1
        if (which === "new") newRecord()
        else if (which === "first") goTo(rowIds.length ? 0 : -1)
        else if (which === "last") goTo(last)
        else if (which === "next") goTo(Math.min(last, position + 1))
        else goTo(Math.max(0, position - 1))
    }
    // Each subform's datasheet: the records linked to this one.
    function openSubforms() {
        for (let i = 0; i < subforms.length && i < 4; ++i) {
            const target = base.subformTable(String(subforms[i].formName || ""))
            const child = String(subforms[i].linkChildFields || "").split(";")[0].trim()
            const master = String(subforms[i].linkMasterFields || "").split(";")[0].trim()
            const key = position < 0 || master.length === 0 || stored[master] === undefined ? null : stored[master]
            if (target.ok) base.subformSheet(i).openLinked(target.table, child, key)
        }
    }
    function closeSession() {
        events.cancel()
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
    onSubformsChanged: openSubforms()

    function reloadRows(at) {
        const filtered = filter.length > 0 ? base.whereRowIds(table, filter) : null
        rowIds = filtered !== null && filtered.ok ? filtered.rowIds : base.formRowIds(table)
        goTo(rowIds.length === 0 ? -1 : Math.max(0, Math.min(at, rowIds.length - 1)))
    }
    function goTo(index, skipEvents) {
        ++events.recordSerial
        events.loading = true
        position = index
        const r = base.formRecord(table, index >= 0 ? Number(rowIds[index]) : 0)
        designer.recordId = r.recordId || ""
        designer.record = r.record || ({})
        stored = r.stored || ({})
        events.previousRecord = Object.assign({}, designer.workingRecord)
        events.loading = false
        openSubforms()
        if (!skipEvents && !opening) events.run("current", "")
    }
    function newRecord() { if (!designing) goTo(-1) }
    function refresh() { bridge.refreshLookups(); reloadRows(position) }
    function saveRecord(done, failed) {
        if (recordEditing) events.requestSave(done, failed)
        else if (failed) failed(qsTr("Switch to Edit Record before saving."))
    }
    function macroRecord() { return bridge.toStored(designer.workingRecord) }
    function setMacroValue(field, input, raw) {
        const lookup = bridge.lookups[field]
        let shown = input
        if (lookup !== undefined) {
            const at = lookup.keys.findIndex(function(key) { return String(key) === String(raw) })
            if (at >= 0) shown = lookup.items[at]
        }
        events.applying = true
        const ok = designer.setFieldValue(field, shown)
        events.applying = false
        return { ok: ok, error: ok ? "" : (designer.requestMessage || qsTr("The field could not be changed.")) }
    }
    function setEventBindings(bindings) {
        const next = Object.assign({}, designDocument.design)
        next.form = Object.assign({}, designDocument.form, { events: bindings })
        const ok = designDocument.setDesign(next)
        return { ok: ok, error: ok ? "" : designDocument.lastError }
    }
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
    Component {
        id: subformComponent
        FormSubform { view: view }
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
                // The title the form shows (the designer edits it only with a
                // design document of its own; FormDesignBridge holds that).
                Tk.TextField {
                    objectName: "formTitleField"
                    visible: view.designing
                    small: true
                    text: view.bridge.presented.title || ""
                    placeholderText: qsTr("Form title")
                    tooltip: qsTr("The title the form shows")
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth * 1.5
                    onEditingFinished: if (text !== (view.bridge.presented.title || "")) view.bridge.applyChange("", { title: text })
                }
                Tk.Spacer { }
                Tk.Badge {
                    visible: view.designDirty
                    variant: "warning"
                    text: qsTr("layout not saved")
                }
                Tk.Chip {
                    objectName: "formFilter"
                    visible: view.filter.length > 0
                    small: true
                    iconName: "funnel"
                    text: qsTr("Filtered: %1").arg(view.filter)
                    tooltip: qsTr("Show all records")
                    onClicked: view.applyFilter("")
                }
            }
        }
        Tk.Notice { visible: view.notice.length > 0; variant: "warning"; text: view.notice; dismissible: true; onDismissed: view.notice = "" }
        Tk.Scroll {
            id: body
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            overflowX: Tk.Scroll.Auto
            accessibleName: view.formName
            Tk.Flex {
                // QindaTK D-316: the implicit width assigned here is what the
                // Scroll measures, so a wide form scrolls sideways.
                implicitWidth: view.cardWidth + Tk.Theme.space.lg * 2
                width: Math.max(body.viewportWidth, implicitWidth)
                direction: Tk.Flex.Column
                align: Tk.Flex.Center
                padding: Tk.Theme.space.lg
                Tk.Card {
                    // Design shows the property sheet beside the controls.
                    width: view.cardWidth
                    padding: Tk.Theme.space.lg
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.md
                        Tk.FormDesigner {
                            id: designer
                            objectName: "baseFormDesigner"
                            provider: mirror
                            tableName: view.table
                            design: view.bridge.presented
                            controlsEnabled: true
                            subformComponent: subformComponent
                            expressionEvaluator: function(expression, record) { return view.evaluate(expression, record) }
                            tooltip: view.title
                            onDesignChangeRequested: function(fieldName, value) { view.bridge.applyChange(fieldName, value) }
                            onControlCommandRequested: function(controlId, actionId) { view.runCommand(controlId, actionId) }
                            onWorkingRecordChanged: {
                                if (view.window !== null) view.window.resetClosePreparation()
                                const before = view.events.previousRecord
                                view.events.previousRecord = Object.assign({}, workingRecord)
                                view.events.changed(before, workingRecord)
                            }
                            onRecordSaveRequested: function(requestId, tableName, recordId, changes) {
                                view.events.save(requestId, tableName, recordId, changes)
                            }
                            onDesignSaveRequested: function(requestId, design) {
                                const ok = view.save()
                                designer.acknowledge(requestId, ok ? "accepted" : "rejected", ok ? qsTr("Layout saved") : qsTr("Not saved"))
                            }
                        }
                    }
                }
                Tk.Flex {
                    objectName: "formFiles"
                    visible: view.fileFields.length > 0 && !view.designing
                    width: Math.min(body.viewportWidth - Tk.Theme.space.lg * 2, Tk.Theme.size.fieldWidth * 4)
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
                    text: view.designing ? qsTr("Design: add controls, drag and size them; Properties edits the selected one")
                                         : qsTr("Changes are saved with Save record, each one undoable")
                    Tk.Flex.shrink: 1
                    elide: Text.ElideRight
                }
            }
        }
    }
}
