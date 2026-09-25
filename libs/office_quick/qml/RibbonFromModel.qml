// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// The window's ribbon (D-041): a Tk.Ribbon whose items are the host's
// ActionEntries, named by id in a per-app `layout`:
//
//   layout: [{ id, label, keyTip, contextId,          // contextId → contextual tab
//              groups: [{ id, label,
//                         items: [spec…] | rows: [[spec…], …] }] }]
//   spec:   "formatBoldAction"                         // small, bound to the entry
//         | { action: "insertTableAction", size: "large", label, keyTip }
//         | { id, component: someComponent }            // a host widget slot
//         | { id, label, iconName, size, split,          // a dropdown; with `action`
//             action, menu: ["idA", "|", "idB"] }       //   and split the body runs it
//
// AGENT-CONTRACT (AGENTS.md rules 4/5): the ribbon is a third view of the
// same ActionEntries as the menu tree and the panel export — it never owns
// a command. An id the model lacks is skipped with one warning (like the
// strips did), never drawn as a dead button. The menu tree stays the
// source of the panel export and of the command search.
Tk.Ribbon {
    id: ribbon

    property ActionModel actions: null
    property DocumentHost host: null
    property var layout: []
    // Contexts the window adds to the host's (QML-derived ones, e.g. Write's
    // "table" while the cursor is in a table).
    property var extraContexts: []

    objectName: "officeRibbon"
    tabs: resolveTabs(layout, actions)
    contextIds: (host !== null ? host.ribbonContexts : []).concat(extraContexts)
    mode: host !== null && host.ribbonCompact ? "compact" : "expanded"
    tooltip: qsTr("Commands")
    trailing: CommandSearch {
        objectName: "ribbonCommandSearch"
        actions: ribbon.actions
    }

    // Home is where a document window starts, when the layout has one.
    Component.onCompleted: if (tabForId("home") !== null) currentTabId = "home"

    function entryFor(id) {
        if (ribbon.actions === null) return null
        const entry = ribbon.actions.entry(id)
        if (entry === null) console.warn("QindaOffice: the ribbon names an unknown command " + id)
        return entry
    }
    function resolveSpec(spec) {
        if (typeof spec === "string") {
            const entry = entryFor(spec)
            return entry === null ? null : { id: spec, action: entry }
        }
        if (spec === null || typeof spec !== "object") return null
        const out = Object.assign({}, spec)
        if (typeof spec.action === "string") {
            const entry = entryFor(spec.action)
            if (entry === null) return null
            out.id = spec.id || spec.action
            out.action = entry
        }
        if (spec.menu) {
            out.menu = spec.menu.map(function(child) {
                if (child === "|") return "|"
                const entry = entryFor(child)
                return entry === null ? null : { id: child, action: entry }
            }).filter(function(child) { return child !== null })
        }
        return out
    }
    function resolveList(list) {
        return (list || []).map(resolveSpec).filter(function(item) { return item !== null })
    }
    function resolveTabs(specs, model) {
        if (model === null) return []
        return (specs || []).map(function(tab) {
            return {
                id: tab.id, label: tab.label, keyTip: tab.keyTip,
                contextual: tab.contextId !== undefined, contextId: tab.contextId,
                groups: (tab.groups || []).map(function(group) {
                    const out = { id: group.id, label: group.label }
                    if (group.rows) out.rows = group.rows.map(resolveList)
                    else out.items = resolveList(group.items)
                    return out
                })
            }
        })
    }
    // ---- introspection for the ribbon gate (tests/support/ribbon_gate.h) ----
    function specsOf(tab) {
        let out = []
        for (const group of (tab.groups || []))
            out = out.concat(group.rows ? [].concat.apply([], group.rows) : (group.items || []))
        return out
    }
    // Every command id the layout reaches — buttons and dropdown entries.
    function commandIds() {
        const ids = []
        const add = function(spec) {
            if (typeof spec === "string") { if (spec !== "|") ids.push(spec); return }
            if (spec && typeof spec.action === "string") ids.push(spec.action)
            if (spec && spec.menu) spec.menu.forEach(add)
        }
        for (const tab of layout) specsOf(tab).forEach(add)
        return ids
    }
    // The `ribbonItem_<id>` names one tab draws.
    function tabItemIds(tabId) {
        const tab = layout.find(function(t) { return t.id === tabId })
        return tab ? specsOf(tab).map(function(spec) {
            return typeof spec === "string" ? spec : String(spec.id || spec.action || "")
        }) : []
    }
    function layoutTabIds() { return layout.map(function(t) { return t.id }) }
    function contextualIds() {
        return layout.filter(function(t) { return t.contextId !== undefined }).map(function(t) { return t.contextId })
    }
    // Icon names the layout asks for that the toolkit's set lacks.
    function unknownIcons() {
        const bad = []
        for (const tab of layout)
            for (const spec of specsOf(tab))
                if (spec && typeof spec === "object" && spec.iconName && !Tk.Icons.has(spec.iconName)) bad.push(spec.iconName)
        return bad
    }
}
