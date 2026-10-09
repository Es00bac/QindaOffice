// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Import an Access .accdb/.mdb file through mdb-tools (only offered when
// its programs are installed — base_access.h). The chosen tables come
// across with their design (types, keys, indexes, required fields,
// descriptions), then the relationships between them and, when asked, the
// saved select queries mdb-queries can rebuild completely
// (base_access_import.h, D-074). The whole import is one undo step.
// Afterwards the dialog turns into the report: what was imported, and
// every table, field, relationship and query left out with the reason.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property url source: ""
    property var tables: []
    property var chosen: []
    property bool queries: true
    property bool queriesAvailable: true
    property string error: ""
    property var report: null // the import's result (AccessImportReport::toMap)
    readonly property bool reporting: report !== null

    title: reporting ? qsTr("Access import report") : qsTr("Import an Access database")
    subtitle: String(source).replace(/^.*\//, "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: reporting ? qsTr("Close") : qsTr("Import")
    secondaryText: reporting ? "" : qsTr("Cancel")
    primaryEnabled: reporting || chosen.length > 0

    function openFor(url) {
        source = url
        report = null
        const tools = base.accessTools()
        queriesAvailable = tools.queries === true
        queries = queriesAvailable
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
    function showReport(r) {
        report = r
        open() // accepting closed the dialog; it comes back as the report
    }
    onAccepted: {
        if (reporting) { report = null; return }
        const r = base.importAccess(source, chosen, queries)
        const done = (r.tables || []).map(function(t) { return qsTr("%1 (%2)").arg(t.table).arg(t.rows) })
        if (done.length > 0) window.notify(qsTr("Imported"), done.join(", "), (r.skipped || []).length ? "warning" : "success")
        Qt.callLater(showReport, r)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { visible: !dialog.reporting && dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.Caption {
            visible: !dialog.reporting
            wrapMode: Text.Wrap
            text: qsTr("Each table comes with its records, field types, key, indexes and required fields, then the relationships between the tables you import. Access forms, reports and macros do not come across.")
        }
        Repeater {
            model: dialog.reporting ? [] : dialog.tables
            delegate: Tk.CheckBox {
                required property string modelData
                objectName: "accessTable_" + modelData
                text: modelData
                checked: dialog.chosen.indexOf(modelData) >= 0
                onToggled: dialog.toggle(modelData, checked)
            }
        }
        Tk.CheckBox {
            objectName: "accessQueries"
            visible: !dialog.reporting
            enabled: dialog.queriesAvailable
            text: dialog.queriesAvailable ? qsTr("Also bring the saved select queries that read one imported table")
                                          : qsTr("Saved queries need mdb-queries, which is not installed")
            checked: dialog.queries
            onToggled: dialog.queries = checked
        }
        // ---- the report ----
        Tk.Notice {
            objectName: "accessReportError"
            visible: dialog.reporting && dialog.report.ok !== true
            variant: "danger"
            text: dialog.reporting ? (dialog.report.error || "") : ""
        }
        Tk.Overline { visible: dialog.reporting && (dialog.report.tables || []).length > 0; title: qsTr("IMPORTED") }
        Repeater {
            model: dialog.reporting ? dialog.report.tables || [] : []
            delegate: Tk.Label {
                required property var modelData
                text: modelData.source === modelData.table
                      ? qsTr("Table %1 — %n record(s)", "", modelData.rows).arg(modelData.table)
                      : qsTr("Table %1 (as %2) — %n record(s)", "", modelData.rows).arg(modelData.source).arg(modelData.table)
            }
        }
        Repeater {
            model: dialog.reporting ? dialog.report.relationships || [] : []
            delegate: Tk.Label {
                required property string modelData
                text: qsTr("Relationship %1").arg(modelData)
            }
        }
        Repeater {
            model: dialog.reporting ? dialog.report.queries || [] : []
            delegate: Tk.Label {
                required property string modelData
                text: qsTr("Query %1").arg(modelData)
            }
        }
        Tk.Overline { visible: dialog.reporting && (dialog.report.notes || []).length > 0; title: qsTr("CHANGED ON THE WAY") }
        Repeater {
            model: dialog.reporting ? dialog.report.notes || [] : []
            delegate: Tk.Caption {
                required property string modelData
                wrapMode: Text.Wrap
                text: modelData
            }
        }
        Tk.Overline {
            objectName: "accessReportSkipped"
            visible: dialog.reporting && (dialog.report.skipped || []).length > 0
            title: qsTr("NOT IMPORTED")
        }
        Repeater {
            model: dialog.reporting ? dialog.report.skipped || [] : []
            delegate: Tk.Caption {
                required property string modelData
                wrapMode: Text.Wrap
                text: modelData
            }
        }
    }
}
