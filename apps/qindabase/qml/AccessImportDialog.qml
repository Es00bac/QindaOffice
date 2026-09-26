// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Import tables from an Access .accdb/.mdb file through mdb-tools (only
// offered when its programs are installed — base_access.h). Each chosen
// table becomes a new table with detected types; the whole import is one
// undo step. Access queries, forms and reports are not converted.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property url source: ""
    property var tables: []
    property var chosen: []
    property string error: ""

    title: qsTr("Import an Access database")
    subtitle: String(source).replace(/^.*\//, "")
    primaryText: qsTr("Import")
    secondaryText: qsTr("Cancel")
    primaryEnabled: chosen.length > 0

    function openFor(url) {
        source = url
        const r = base.accessTables(url)
        tables = r.ok ? r.tables : []
        chosen = tables.slice()
        error = r.ok ? "" : r.error
        open()
    }
    function toggle(name, on) {
        const next = chosen.filter(function(t) { return t !== name })
        if (on) next.push(name)
        chosen = next
    }
    onAccepted: {
        const r = base.importAccess(source, chosen)
        const done = (r.imported || []).map(function(t) { return qsTr("%1 (%2)").arg(t.table).arg(t.rows) })
        if (done.length > 0) window.notify(qsTr("Imported"), done.join(", "), r.ok ? "success" : "warning")
        if (!r.ok) window.confirmDialog.ask(qsTr("Some tables were not imported"), r.error, "warning", qsTr("OK"), null)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("Tables and their records come across; Access queries, forms, reports and relationships do not. Recreate relationships in Database Tools ▸ Relationships.")
        }
        Repeater {
            model: dialog.tables
            delegate: Tk.CheckBox {
                required property string modelData
                text: modelData
                checked: dialog.chosen.indexOf(modelData) >= 0
                onToggled: dialog.toggle(modelData, checked)
            }
        }
    }
}
