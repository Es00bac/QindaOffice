// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Between QindaTK's FormDesigner and the form's design document (M89,
// D-097). The designer runs on its host-owned path (no designDocument):
// what it shows is `presented` — the document's form with what is not design
// data filled in (a lookup combo or list box's items: the related records'
// labels, refreshed each time the form opens or is refreshed) — and every
// change it asks for (one per finished gesture or committed property,
// QindaTK D-311) becomes ONE command on the document, whose history the
// window's Undo and Redo walk and whose revision says whether the layout is
// saved.
//
// AGENT-GUARD: host-filled `items` never reach the document. A control bound
// to a lookup field keeps the items the document already had (none, as laid
// out); without this a save would freeze today's related records into the
// form, and every refresh would be an undo step.
QtObject {
    id: bridge

    required property var view              // FormView
    readonly property var document: view.designDocument
    readonly property var base: view.base
    property int revision: 0                // bumps on every document change
    property var lookups: ({})              // field → {items, keys}
    readonly property var presented: {
        bridge.revision
        return bridge.present(bridge.document, bridge.lookups)
    }
    readonly property Connections watcher: Connections {
        target: bridge.document
        function onChanged() { ++bridge.revision }
    }

    // A lookup's items: the labels, after a blank item (no related record)
    // unless the field is required.
    function refreshLookups() {
        const next = ({})
        const all = view.table.length > 0 ? base.formLookups(view.table) : ({})
        for (const field in all) {
            const r = all[field]
            // AGENT-GUARD: Qt's sequence wrappers are array-like, but concat
            // appends a wrapper as one object. Copy them before adding the
            // optional blank, or the designer rejects every later gesture.
            const items = Array.from(r.items || [])
            const keys = Array.from(r.keys || [])
            next[field] = r.required ? { items: items, keys: keys }
                                     : { items: [""].concat(items), keys: [null].concat(keys) }
        }
        lookups = next
        ++revision
    }
    function boxed(control) {
        return (control.type === "comboBox" || control.type === "listBox")
            && lookups[String(control.fieldName || "")] !== undefined
    }
    function present(doc, lookupsNow) {
        if (doc === null) return ({ title: "", fields: [], controls: [] })
        const form = doc.form
        const controls = (form.controls || []).map(function(c) {
            const field = String(c.fieldName || "")
            if ((c.type === "comboBox" || c.type === "listBox") && lookupsNow[field] !== undefined)
                return Object.assign({}, c, { items: lookupsNow[field].items })
            return c
        })
        return Object.assign({}, form, { controls: controls })
    }

    // ---- record values: a lookup's label ↔ its key ----
    function keyFor(field, value) {
        const l = lookups[field]
        if (l === undefined || value === null || value === undefined) return value
        const at = l.items.indexOf(String(value))
        return at >= 0 ? l.keys[at] : value
    }
    // A record as the designer holds it, with each lookup's key for its label.
    function toStored(record) {
        const out = ({})
        for (const name in record) out[name] = keyFor(name, record[name])
        return out
    }

    // ---- design changes: one document command each ----
    function controlById(list, id) {
        for (let i = 0; i < list.length; ++i) if (String(list[i].id) === String(id)) return list[i]
        return null
    }
    // A value as text whatever the key order of its maps.
    function canonical(v) {
        if (v === null || v === undefined) return "null"
        if (Array.isArray(v)) return "[" + v.map(canonical).join(",") + "]"
        if (typeof v === "object")
            return "{" + Object.keys(v).sort().map(function(k) { return JSON.stringify(k) + ":" + canonical(v[k]) }).join(",") + "}"
        return JSON.stringify(v)
    }
    function stored(control, before) {
        if (!boxed(control)) return control
        const out = Object.assign({}, control)
        const was = controlById(before, control.id)
        if (was !== null && was.items !== undefined) out.items = was.items
        else delete out.items
        return out
    }
    // The properties of `after` that differ from `before` (null removes one).
    function changesOf(before, after) {
        const out = ({})
        for (const key in after) if (canonical(after[key]) !== canonical(before[key])) out[key] = after[key]
        for (const key in before) if (after[key] === undefined) out[key] = null
        return out
    }
    function applyControls(next) {
        const doc = document
        const before = doc.form.controls || []
        const after = next.map(function(c) { return bridge.stored(c, before) })
        const added = after.filter(function(c) { return bridge.controlById(before, c.id) === null })
        const removed = before.filter(function(c) { return bridge.controlById(after, c.id) === null })
        const changed = after.filter(function(c) {
            const was = bridge.controlById(before, c.id)
            return was !== null && bridge.canonical(was) !== bridge.canonical(c)
        })
        // The command that says what happened ("Add Combo box", "Move or
        // size Text3", "Delete Button1"), else the whole list as one step.
        if (added.length === 1 && removed.length === 0 && changed.length === 0 && after[after.length - 1].id === added[0].id)
            return doc.addFormControl(added[0]).length > 0
        if (added.length === 0 && changed.length === 0 && removed.length > 0
                && removed.every(function(c) { return c.id === removed[0].id || c.parentId === removed[0].id }))
            return doc.removeFormControl(removed[0].id)
        if (added.length === 0 && removed.length === 0 && changed.length === 1)
            return doc.setFormControl(changed[0].id, changesOf(bridge.controlById(before, changed[0].id), changed[0]))
        if (added.length === 0 && removed.length === 0 && changed.length === 0) return true
        return doc.setFormControls(after)
    }
    // FormDesigner.designChangeRequested(fieldName, value) on the host path.
    function applyChange(fieldName, value) {
        const doc = document
        if (doc === null) return false
        let done = true
        if (fieldName.length > 0) {
            if (value.label !== undefined) done = doc.setFormFieldLabel(fieldName, value.label)
            else if (value.hidden !== undefined) done = doc.setFormFieldHidden(fieldName, value.hidden)
            else if (value.index !== undefined) done = doc.moveFormField(fieldName, value.index)
        } else if (value.title !== undefined) {
            done = doc.setFormTitle(value.title)
        } else if (value.controls !== undefined) {
            done = applyControls(value.controls)
        }
        if (!done) view.window.notify(qsTr("Form not changed"), doc.lastError, "warning")
        return done
    }
}
