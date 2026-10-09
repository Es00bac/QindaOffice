// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Runs a macro's actions in turn (M80, D-089 §5) — a named macro, the
// macro designer's draft or a form button's embedded actions. Each action
// goes through the window's own commands (BaseCommands.qml), so an action
// query still shows its count and asks first, a report still asks for its
// parameters, and a designer with unsaved edits is still asked about; the
// next action starts when that one is done. A failing action stops the
// macro and says which step stopped it.
//
// AGENT-CONTRACT (with base_macro.h's catalog): every action id there is
// handled in `next`; an id added there must be added here.
QtObject {
    id: runner

    property var window: null
    readonly property var commands: window !== null ? window.commands : null
    readonly property var base: window !== null ? window.base : null
    property var queue: []
    property string label: ""
    property int step: 0
    property int depth: 0
    property int generation: 0 // a newer run abandons the continuations of an older one
    property bool running: false
    readonly property int maxDepth: 20

    function run(actions, name) {
        ++generation
        queue = (actions || []).map(function(a) { return { action: a.action, arguments: a.arguments || ({}) } })
        label = name || qsTr("Macro")
        step = 0
        depth = 0
        running = true
        next()
    }
    function runNamed(name) {
        const m = base.macroDesign(name)
        if (!m.ok) { window.notify(qsTr("The macro did not run"), m.error, "danger"); return }
        const check = base.checkMacro(m, name)
        if (!check.ok) { window.notify(qsTr("%1 cannot run").arg(name), check.problems.join("\n"), "warning"); return }
        run(m.actions, name)
    }
    function stop(problem) {
        if (problem) window.notify(qsTr("%1 stopped at step %2").arg(label).arg(step), problem, "warning")
        queue = []
        running = false
    }
    function next() {
        if (!running) return
        if (queue.length === 0) { running = false; return }
        const s = queue[0]
        queue = queue.slice(1)
        ++step
        const a = s.arguments || ({})
        const mine = generation
        const done = function() { if (mine === runner.generation) Qt.callLater(runner.next) }
        const failed = function(problem) { if (mine === runner.generation) runner.stop(problem) }
        switch (s.action) {
        case "openForm": commands.openFormFor(a.form, a.where || "", a.dataMode || "edit", done, failed); return
        case "openReport": commands.openReportFor(a.report, a.view || "preview", done, failed); return
        case "openQuery": commands.runQuery(a.query, ({}), ({}), done, failed); return
        case "openTable": commands.openTableFor(a.table, done, failed); return
        case "applyFilter":
        case "showAllRecords": {
            const r = commands.applyFilter(s.action === "applyFilter" ? (a.where || "") : "")
            if (r.ok) done(); else failed(r.error)
            return
        }
        case "goToRecord": commands.goToRecord(a.record || "next"); done(); return
        case "saveRecord": commands.recordTarget().saveRecord(); done(); return
        case "requery": commands.recordTarget().refresh(); done(); return
        case "findRecord": {
            const found = commands.findRecord(a.text || "")
            if (found.ok) done(); else failed(found.error)
            return
        }
        case "closeWindow": commands.closeObject(done); return
        case "messageBox":
            window.confirmDialog.ask(a.title || qsTr("QindaBase"), a.message || "", "info", qsTr("OK"), done, "",
                                     function() { runner.stop("") })
            return
        case "runMacro": {
            if (depth >= maxDepth) { failed(qsTr("Macros run each other more than %1 deep.").arg(maxDepth)); return }
            const m = base.macroDesign(a.macro || "")
            if (!m.ok) { failed(m.error); return }
            ++depth
            queue = m.actions.concat(queue)
            done()
            return
        }
        case "stopMacro": stop(""); return
        }
        failed(qsTr("QindaBase has no macro action %1.").arg(s.action))
    }
}
