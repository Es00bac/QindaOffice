// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The macro designer (M80, D-089 §5): Access's macros without VBA — a
// list of actions (open a form or report, run a query, apply a filter, go
// to a record, show a message, …), each with its arguments, run top to
// bottom. What keeps the macro from running is listed as it is edited
// (BaseService.checkMacro). Designer edits have their own undo while the
// view is open; Save stores the macro as one undo step of the database.
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var macro: ({ description: "", actions: [] })
    property string original: "{}"
    property string name: ""
    property int current: -1
    property var history: []
    property var future: []
    readonly property bool dirty: JSON.stringify(macro) !== original
    readonly property var problems: base !== null && window.view === "macroDesign" ? base.checkMacro(macro, name).problems : []

    function load(map) {
        if (!map || map.ok === false) { window.notify(qsTr("Cannot design"), map ? map.error : "", "danger"); return }
        name = map.name || ""
        macro = { description: map.description || "", actions: map.actions || [] }
        original = JSON.stringify(macro)
        current = macro.actions.length > 0 ? 0 : -1
        history = []
        future = []
        syncHost()
    }
    function syncHost() {
        if (window === null || window.baseHost === null || window.view !== "macroDesign") return
        window.baseHost.designDirty = dirty || name.length === 0 && macro.actions.length > 0
        window.baseHost.designCanUndo = history.length > 0
        window.baseHost.designCanRedo = future.length > 0
    }
    onDirtyChanged: syncHost()
    onVisibleChanged: if (visible) syncHost()

    function edit(mutate) {
        const next = JSON.parse(JSON.stringify(macro))
        mutate(next)
        history = history.concat([JSON.stringify(macro)]).slice(-100)
        future = []
        macro = next
        syncHost()
    }
    function undo() {
        if (history.length === 0) return
        future = future.concat([JSON.stringify(macro)])
        macro = JSON.parse(history[history.length - 1])
        history = history.slice(0, -1)
        current = Math.min(current, macro.actions.length - 1)
        syncHost()
    }
    function redo() {
        if (future.length === 0) return
        history = history.concat([JSON.stringify(macro)])
        macro = JSON.parse(future[future.length - 1])
        future = future.slice(0, -1)
        syncHost()
    }
    function addAction(id) {
        const at = current < 0 ? macro.actions.length : current + 1
        edit(function(m) { m.actions.splice(at, 0, { action: id || "openForm", arguments: ({}) }) })
        current = at
    }
    function removeAction(index) {
        if (index < 0 || index >= macro.actions.length) return
        edit(function(m) { m.actions.splice(index, 1) })
        current = Math.min(index, macro.actions.length - 1)
    }
    function moveAction(index, delta) {
        const to = index + delta
        if (index < 0 || to < 0 || to >= macro.actions.length) return
        edit(function(m) { const step = m.actions.splice(index, 1)[0]; m.actions.splice(to, 0, step) })
        current = to
    }
    function command(id) {
        switch (id) {
        case "baseMacroAddActionAction": addAction(""); break
        case "baseMacroDeleteActionAction": removeAction(current); break
        case "baseMacroMoveUpAction": moveAction(current, -1); break
        case "baseMacroMoveDownAction": moveAction(current, 1); break
        case "baseMacroRunAction": run(); break
        }
    }
    function run() {
        if (problems.length > 0) { window.notify(qsTr("The macro cannot run yet"), problems.join("\n"), "warning"); return }
        window.commands.macroRunner.run(macro.actions, name || qsTr("New macro"))
    }
    function save() {
        let target = name
        if (target.length === 0) {
            let n = base.macroNames.length + 1
            while (base.macroNames.indexOf(qsTr("Macro%1").arg(n)) >= 0) ++n
            target = qsTr("Macro%1").arg(n)
        }
        const r = base.saveMacro(target, macro)
        if (r.needsSystemTables) {
            window.confirmDialog.ask(qsTr("Add QindaBase's tables?"), r.error, "question", qsTr("Add them"), function() {
                base.addSystemTables(); if (save()) window.baseHost.designDirty = false
            })
            return false
        }
        if (!r.ok) { window.notify(qsTr("Macro not saved"), r.error, "danger"); return false }
        name = r.name
        original = JSON.stringify(macro)
        window.baseHost.currentName = r.name
        window.baseHost.selectedKind = "macro"
        window.baseHost.selectedName = r.name
        syncHost()
        return true
    }
    function discard() { load(original === "{}" ? base.newMacroDesign() : Object.assign(JSON.parse(original), { name: name, ok: true })) }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "macroDesignHeader"
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
                Tk.Icon { name: "workflow"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                Tk.Heading { objectName: "macroDesignName"; level: 3; text: view.name || qsTr("New macro"); Tk.Flex.shrink: 0 }
                Tk.TextField {
                    objectName: "macroDescription"
                    small: true
                    text: view.macro.description || ""
                    placeholderText: qsTr("What it does")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    onEditingFinished: { const t = text; if (t !== (view.macro.description || "")) view.edit(function(m) { m.description = t }) }
                }
                Tk.Button { text: qsTr("Save"); iconName: "save"; enabled: view.dirty || view.name.length === 0; onClicked: view.save() }
                Tk.Button {
                    objectName: "macroRun"
                    text: qsTr("Run"); iconName: "play"; variant: "accent"
                    enabled: view.macro.actions.length > 0
                    onClicked: view.run()
                }
            }
        }
        Tk.Scroll {
            id: body
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            accessibleName: qsTr("Macro design")
            Tk.Flex {
                width: body.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                padding: Tk.Theme.space.md
                Tk.Notice {
                    objectName: "macroProblems"
                    visible: view.problems.length > 0
                    variant: "warning"
                    title: qsTr("Before it can run")
                    text: view.problems.join("\n")
                }
                Tk.Label {
                    visible: view.macro.actions.length === 0
                    text: qsTr("No actions yet. Add Action puts one under the selected action; each runs in turn, top to bottom.")
                    muted: true
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: view.macro.actions
                    delegate: MacroActionRow {
                        required property var modelData
                        required property int index
                        width: body.width - Tk.Theme.space.md * 2
                        base: view.base
                        step: modelData
                        stepIndex: index
                        selected: index === view.current
                        canMoveUp: index > 0
                        canMoveDown: index + 1 < view.macro.actions.length
                        onPicked: view.current = index
                        onEdited: function(next) { const at = index; view.edit(function(m) { m.actions[at] = next }) }
                        onRemoved: view.removeAction(index)
                        onMoved: function(delta) { view.moveAction(index, delta) }
                    }
                }
                Tk.Button {
                    objectName: "macroAddAction"
                    text: qsTr("Add action")
                    iconName: "list-plus"
                    small: true
                    onClicked: view.addAction("")
                }
            }
        }
    }
}
