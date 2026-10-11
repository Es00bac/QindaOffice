// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window-owned macro execution. Every operation uses BaseCommands or the
// current form draft; action queries retain their preview and Undo contract.
// Event runs complete through done/failed so BeforeUpdate cannot save early.
QtObject {
    id: runner
    property var window: null
    readonly property var commands: window !== null ? window.commands : null
    readonly property var base: window !== null ? window.base : null
    property var queue: []
    property string label: ""
    property int step: 0
    property int depth: 0
    property int generation: 0
    property bool running: false
    property var eventForm: null
    property var completed: null
    property var rejected: null
    readonly property int maxDepth: 20
    readonly property var form: eventForm !== null ? eventForm
        : window !== null && window.view === "form" ? window.formView : null

    function run(actions, name, done, failed, context) {
        if (running) stop(qsTr("A newer macro replaced this run."))
        const check = base.checkMacro({ actions: actions || [] }, "")
        if (!check.ok) {
            if (failed) failed((check.problems || [check.error || qsTr("The macro is unavailable.")]).join("\n"))
            else window.notify(qsTr("The macro cannot run"), (check.problems || [check.error || qsTr("The macro is unavailable.")]).join("\n"), "warning")
            return
        }
        ++generation
        queue = Array.from(actions || []).map(function(a) {
            return { action: a.action, arguments: a.arguments || ({}) }
        })
        label = name || qsTr("Macro")
        step = 0
        depth = 0
        eventForm = context || null
        completed = done || null
        rejected = failed || null
        running = true
        next()
    }
    function runNamed(name, done, failed, context) {
        const m = base.macroDesign(name)
        if (!m.ok) {
            if (failed) failed(m.error)
            else window.notify(qsTr("The macro did not run"), m.error, "danger")
            return
        }
        const check = base.checkMacro(m, name)
        if (!check.ok) {
            if (failed) failed((check.problems || [check.error || qsTr("The macro is unavailable.")]).join("\n"))
            else window.notify(qsTr("%1 cannot run").arg(name), (check.problems || [check.error || qsTr("The macro is unavailable.")]).join("\n"), "warning")
            return
        }
        run(m.actions, name, done, failed, context)
    }
    function finish(ok, problem) {
        const callback = ok ? completed : rejected
        queue = []
        running = false
        completed = null
        rejected = null
        eventForm = null
        if (callback) callback(problem || "")
        else if (!ok && problem) window.notify(qsTr("%1 stopped at step %2").arg(label).arg(step), problem, "warning")
    }
    function stop(problem) { finish(false, problem) }
    function value(expression, field) {
        const target = form
        return base.evaluateMacroValue(target !== null ? target.table : "", expression,
                                       target !== null ? target.macroRecord() : ({}),
                                       commands.macroTemporaryValues, field || "")
    }
    // Discard an unchosen branch, respecting nested If blocks. Programs
    // are checked before execution, including each called named macro.
    function skipBranch(allowElse) {
        let nested = 0
        while (queue.length) {
            const action = queue[0].action
            queue = queue.slice(1)
            if (action === "if") ++nested
            else if (action === "endIf") {
                if (nested === 0) return true
                --nested
            } else if (action === "else" && nested === 0 && allowElse) return true
        }
        return false
    }
    function next() {
        if (!running) return
        if (queue.length === 0) { finish(true, ""); return }
        const s = queue[0]
        queue = queue.slice(1)
        if (++step > 1024) { stop(qsTr("A macro run is limited to 1024 actions.")); return }
        const a = s.arguments || ({})
        const mine = generation
        const done = function() { if (mine === runner.generation && runner.running) Qt.callLater(runner.next) }
        const failed = function(problem) { if (mine === runner.generation && runner.running) runner.stop(problem) }
        switch (s.action) {
        case "__return": --depth; done(); return
        case "if": {
            const r = value(a.condition || "", "")
            if (!r.ok) { failed(r.error); return }
            if (!r.value && !skipBranch(true)) { failed(qsTr("If has no matching EndIf.")); return }
            done(); return
        }
        case "else":
            if (!skipBranch(false)) { failed(qsTr("Else has no matching EndIf.")); return }
            done(); return
        case "endIf": done(); return
        case "setTempVar": {
            const name = String(a.name || "").trim()
            if (!name.length || name.length > 128 || ["__proto__", "prototype", "constructor"].indexOf(name.toLowerCase()) >= 0) {
                failed(qsTr("A temporary variable needs a non-reserved name of at most 128 characters.")); return
            }
            const r = value(a.expression || "", "")
            if (!r.ok) { failed(r.error); return }
            const variables = Object.assign({}, commands.macroTemporaryValues)
            for (const key in variables) if (key.toLowerCase() === name.toLowerCase()) delete variables[key]
            if (Object.keys(variables).length >= 128) { failed(qsTr("This window already has 128 temporary variables.")); return }
            variables[name] = { value: r.value, type: r.type }
            commands.macroTemporaryValues = variables
            done(); return
        }
        case "removeTempVar": {
            const variables = Object.assign({}, commands.macroTemporaryValues)
            for (const key in variables) if (key.toLowerCase() === String(a.name || "").toLowerCase()) delete variables[key]
            commands.macroTemporaryValues = variables
            done(); return
        }
        case "setValue": {
            if (form === null || form.designing) { failed(qsTr("Open a form in record mode before SetValue.")); return }
            let field = String(a.field || "").trim()
            if (field.startsWith("[") && field.endsWith("]")) field = field.slice(1, -1)
            const r = value(a.expression || "", field)
            if (!r.ok) { failed(r.error); return }
            const changed = form.setMacroValue(field, r.input, r.value)
            if (changed.ok) done(); else failed(changed.error)
            return
        }
        case "cancelEvent":
            if (eventForm === null) { failed(qsTr("CancelEvent is available only in a form event.")); return }
            failed(a.message || qsTr("The form event was cancelled.")); return
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
        case "saveRecord":
            if (form !== null) form.saveRecord(done, failed)
            else { commands.recordTarget().saveRecord(); done() }
            return
        case "requery": commands.recordTarget().refresh(); done(); return
        case "findRecord": {
            const found = commands.findRecord(a.text || "")
            if (found.ok) done(); else failed(found.error)
            return
        }
        case "closeWindow": commands.closeObject(done, failed); return
        case "messageBox":
            window.confirmDialog.ask(a.title || qsTr("QindaBase"), a.message || "", "info", qsTr("OK"), done, "",
                                     function() { failed(qsTr("The message was cancelled.")) })
            return
        case "runMacro": {
            if (depth >= maxDepth) { failed(qsTr("Macros run each other more than %1 deep.").arg(maxDepth)); return }
            const m = base.macroDesign(a.macro || "")
            if (!m.ok) { failed(m.error); return }
            const check = base.checkMacro(m, a.macro || "")
            if (!check.ok) { failed((check.problems || [check.error || qsTr("The macro is unavailable.")]).join("\n")); return }
            ++depth
            queue = Array.from(m.actions).concat([{ action: "__return" }], queue)
            done(); return
        }
        case "stopMacro": finish(true, ""); return
        }
        failed(qsTr("QindaBase has no macro action %1.").arg(s.action))
    }
}
