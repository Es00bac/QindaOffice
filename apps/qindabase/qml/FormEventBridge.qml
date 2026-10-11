// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Named event macros share the form draft and the window's TempVars. This
// runner is separate from a command macro so OpenForm can await its events
// without abandoning the calling macro's continuation.
QtObject {
    id: events
    required property var view
    readonly property var bindings: view.designDocument !== null ? (view.designDocument.form.events || ({})) : ({})
    readonly property Component runnerFactory: Component { MacroRunner { window: events.view.window } }
    property var pool: []
    property var activeKeys: ({})
    readonly property bool running: Object.keys(activeKeys).length > 0
    property bool loading: false
    property bool applying: false
    property bool saving: false
    property int recordSerial: 0
    property var previousRecord: ({})
    property var saveDone: null
    property var saveFailed: null

    function name(event, control) {
        const scope = control.length ? ((bindings.controls || ({}))[control] || ({})) : (bindings.form || ({}))
        return String(scope[event] || "")
    }
    function problem(text) { view.window.notify(qsTr("Form event stopped"), text, "warning") }
    function run(event, control, done, failed) {
        runMany([{ event: event, control: control || "" }], done, failed)
    }
    function runMany(jobs, done, failed) {
        const complete = done || function() {}
        const refused = failed || problem
        if (!view.recordEditing) { complete(); return }
        const pending = jobs.filter(function(job) { return events.name(job.event, job.control || "").length > 0 })
        if (!pending.length) { complete(); return }
        // A button event may close a form, invoking its Close event. Give
        // each nested event its own continuation, but reject recursion onto
        // the same event. Reuse at most 20 runners for the window lifetime;
        // delayed dialog callbacks keep a valid generation-guarded object.
        let entry = pool.find(function(candidate) { return !candidate.busy })
        if (!entry) {
            if (pool.length >= 20) { refused(qsTr("Form events are limited to 20 nested runs.")); return }
            entry = { worker: runnerFactory.createObject(events), busy: false, key: "" }
            pool = pool.concat([entry])
        }
        entry.busy = true
        const clearKey = function() {
            if (!entry.key.length) return
            const keys = Object.assign({}, events.activeKeys)
            delete keys[entry.key]
            events.activeKeys = keys
            entry.key = ""
        }
        const release = function() { clearKey(); entry.busy = false }
        const stop = function(error) { release(); refused(error) }
        let index = 0
        const next = function() {
            clearKey()
            while (index < pending.length) {
                const job = pending[index++]
                const macro = events.name(job.event, job.control || "")
                if (macro.length) {
                    const key = view.formName + "/" + (job.control || "") + "/" + job.event
                    if (events.activeKeys[key]) { stop(qsTr("The %1 event called itself.").arg(job.event)); return }
                    entry.key = key
                    const keys = Object.assign({}, events.activeKeys)
                    keys[key] = true
                    events.activeKeys = keys
                    entry.worker.runNamed(macro, next, stop, events.view)
                    return
                }
            }
            release()
            complete()
        }
        next()
    }
    function controlsFor(event, fields) {
        const result = []
        for (const c of (view.bridge.presented.controls || [])) {
            if (fields.indexOf(String(c.fieldName || "")) >= 0 && name(event, String(c.id)).length)
                result.push({ event: event, control: String(c.id) })
        }
        return result
    }
    function recordEvents(event, fields, done, failed) {
        runMany(controlsFor(event, fields).concat([{ event: event, control: "" }]), done, failed)
    }
    function changed(before, after) {
        if (loading || applying || !view.recordEditing) return
        const fields = Object.keys(after).filter(function(key) { return JSON.stringify(before[key]) !== JSON.stringify(after[key]) })
        const jobs = controlsFor("change", fields)
        if (jobs.length) runMany(jobs)
    }
    function cancel() {
        for (const entry of pool.slice())
            if (entry.busy) entry.worker.stop(qsTr("The form was closed before its event finished."))
    }
    function requestSave(done, failed) {
        if (saving) { if (failed) failed(qsTr("A record save is already pending.")); return }
        saveDone = done || null
        saveFailed = failed || null
        const id = view.formDesigner.requestRecordSave()
        if (!id.length) finishSave(false, view.formDesigner.requestMessage || qsTr("The record cannot be saved."))
    }
    function finishSave(ok, error) {
        saving = false
        const callback = ok ? saveDone : saveFailed
        saveDone = null
        saveFailed = null
        if (callback) callback(error || "")
        else if (!ok && error) problem(error)
    }
    function save(requestId, tableName, recordId, changes) {
        const formName = view.formName
        const serial = recordSerial
        const fields = Object.keys(changes)
        const reject = function(error) {
            view.formDesigner.acknowledge(requestId, "rejected", error)
            events.finishSave(false, error)
        }
        if (saving) { reject(qsTr("A record save is already pending.")); return }
        saving = true
        recordEvents("beforeUpdate", fields, function() {
            if (serial !== events.recordSerial || formName !== view.formName) {
                reject(qsTr("The form moved to another record before the update completed.")); return
            }
            // A BeforeUpdate SetValue may have changed another field.
            const pending = ({})
            const editor = view.formDesigner
            for (const key in editor.workingRecord)
                if (JSON.stringify(editor.workingRecord[key]) !== JSON.stringify(editor.record[key])) pending[key] = editor.workingRecord[key]
            const changedFields = Object.keys(pending)
            const r = view.base.saveFormRecord(tableName, Number(recordId) || 0, view.bridge.toStored(pending))
            editor.acknowledge(requestId, r.ok ? "accepted" : "rejected", r.ok ? qsTr("Saved") : r.error)
            if (!r.ok) { events.finishSave(false, r.error); return }
            view.rowIds = view.base.formRowIds(view.table)
            view.goTo(Math.max(0, Array.from(view.rowIds).map(Number).indexOf(Number(r.rowid))), true)
            events.recordEvents("afterUpdate", changedFields, function() { events.finishSave(true, "") },
                                function(error) { events.finishSave(false, qsTr("The record was saved, but After Update failed: %1").arg(error)) })
        }, reject)
    }
}
