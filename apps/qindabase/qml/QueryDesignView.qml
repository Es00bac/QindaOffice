// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The query designer: the tables and how they join, the design grid
// (Access's rows: field, name, total, crosstab, sort, show, criteria, or —
// and update to / append to for action queries), and the query's SQL.
// SQL view shows the statement in Access SQL and edits it there: what the
// grid can hold goes back into the grid (one undo step), anything else can
// be kept as SQLite SQL (M80, D-089 §4). Query types: select, update,
// delete, append, crosstab and make-table; Query Parameters declares the
// prompts. Running a draft needs no save; action and make-table queries
// always say what they will change first.
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var design: ({ kind: "select", tables: [], joins: [], columns: [], totals: false })
    property string original: "{}"
    property string name: ""
    property var history: []
    property var future: []
    property string mode: "design" // design | sql
    property string sqlText: ""     // the Access SQL being edited in SQL view
    property string sqlError: ""    // why that text does not fit the grid
    readonly property bool dirty: JSON.stringify(design) !== original
    readonly property bool sqlOnly: design.kind === "sql"
    readonly property var compiled: base !== null && window.view === "queryDesign" ? base.compileQuery(runnable()) : ({})
    readonly property string accessText: base !== null && window.view === "queryDesign" && !sqlOnly
                                         ? base.accessSql(runnable()).sql : ""
    readonly property real rowHeight: Tk.Theme.size.control
    readonly property bool selectLike: design.kind === "select" || design.kind === "makeTable"
    readonly property var fieldChoices: {
        const out = []
        for (const t of design.tables || []) {
            out.push(t.name + ".*")
            for (const f of view.base.tableFields(t.name)) out.push(t.name + "." + f.name)
        }
        return out
    }
    readonly property var rowLabels: {
        const out = [qsTr("Field"), qsTr("Name")]
        const crosstab = design.kind === "crosstab"
        if ((design.totals && selectLike) || crosstab) out.push(qsTr("Total"))
        if (crosstab) out.push(qsTr("Crosstab"))
        if (selectLike || crosstab) out.push(qsTr("Sort"))
        if (selectLike) out.push(qsTr("Show"))
        if (design.kind === "update") out.push(qsTr("Update to"))
        if (design.kind === "append") out.push(qsTr("Append to"))
        out.push(qsTr("Criteria"), qsTr("Or"))
        return out
    }

    // The design without empty grid columns.
    function runnable() {
        const d = Object.assign({}, design)
        d.columns = (design.columns || []).filter(function(c) { return (c.field || "").length > 0 || (c.expression || "").trim().length > 0 })
        return d
    }
    function load(map) {
        if (!map || map.ok === false) { window.notify(qsTr("Cannot design"), map ? map.error : "", "danger"); return }
        name = map.name || ""
        const d = Object.assign({}, map)
        delete d.ok
        delete d.name
        design = d
        original = JSON.stringify(d)
        history = []
        future = []
        mode = d.kind === "sql" ? "sql" : "design"
        sqlError = ""
        syncHost()
    }
    function syncHost() {
        if (window === null || window.baseHost === null || window.view !== "queryDesign") return
        const host = window.baseHost
        host.designDirty = dirty || name.length === 0 && (design.columns || []).length > 0
        host.designCanUndo = history.length > 0
        host.designCanRedo = future.length > 0
        host.setChecked("baseQueryTotalsAction", design.totals === true)
        const kind = design.kind === "sql" ? "select" : design.kind
        host.setChecked("baseQueryType" + kind.charAt(0).toUpperCase() + kind.slice(1) + "Action", true)
    }
    onDirtyChanged: syncHost()
    onVisibleChanged: if (visible) syncHost()

    function edit(mutate) {
        const next = JSON.parse(JSON.stringify(design))
        mutate(next)
        history = history.concat([JSON.stringify(design)]).slice(-100)
        future = []
        design = next
        syncHost()
    }
    function undo() {
        if (history.length === 0) return
        future = future.concat([JSON.stringify(design)])
        design = JSON.parse(history[history.length - 1])
        history = history.slice(0, -1)
        sqlText = accessText
        syncHost()
    }
    function redo() {
        if (future.length === 0) return
        history = history.concat([JSON.stringify(design)])
        design = JSON.parse(future[future.length - 1])
        future = future.slice(0, -1)
        sqlText = accessText
        syncHost()
    }
    function addTable(table) {
        edit(function(d) {
            if (d.tables.some(function(t) { return t.name === table })) return
            d.tables.push({ name: table, alias: "" })
            const names = d.tables.map(function(t) { return t.name })
            for (const j of view.base.suggestJoins(names)) {
                if (!d.joins.some(function(x) { return x.leftTable === j.leftTable && x.rightTable === j.rightTable && x.leftField === j.leftField }))
                    d.joins.push(j)
            }
        })
    }
    function removeTable(table) {
        edit(function(d) {
            d.tables = d.tables.filter(function(t) { return t.name !== table })
            d.joins = d.joins.filter(function(j) { return j.leftTable !== table && j.rightTable !== table })
            d.columns = d.columns.filter(function(c) { return c.table !== table })
        })
    }
    function blankColumn(table, field, expression) {
        return { table: table, field: field, expression: expression, alias: "", total: "groupBy", sort: 0, show: true,
                 criteria: "", orCriteria: [], updateTo: "", appendTo: "", crosstab: "" }
    }
    function addColumn(table, field) { edit(function(d) { d.columns.push(view.blankColumn(table, field, "")) }) }

    // ---- SQL view (Access SQL both ways) ----
    function showSql() {
        if (mode === "sql") return
        sqlText = accessText
        sqlError = ""
        mode = "sql"
    }
    // Puts the edited Access SQL back into the grid; false (with sqlError)
    // when the grid cannot hold it.
    function applySql() {
        if (sqlOnly || mode !== "sql" || sqlText.trim() === accessText.trim()) return true
        const r = base.parseAccessSql(sqlText)
        if (!r.ok) { sqlError = r.error; return false }
        sqlError = ""
        edit(function(d) {
            for (const key of Object.keys(d)) delete d[key]
            Object.assign(d, r.design)
        })
        sqlText = accessText
        return true
    }
    function showDesign() {
        if (mode === "design") return
        if (sqlOnly) {
            const r = base.parseAccessSql(design.sql || "")
            if (!r.ok) {
                window.notify(qsTr("SQL view only"), qsTr("This query is SQLite SQL the design grid cannot show: %1").arg(r.error), "info")
                return
            }
            edit(function(d) { for (const key of Object.keys(d)) delete d[key]; Object.assign(d, r.design) })
        } else if (!applySql()) {
            return
        }
        mode = "design"
    }
    // Statements the grid cannot hold run as they are, in SQLite.
    function keepAsSqlite() {
        const text = sqlText
        edit(function(d) { d.kind = "sql"; d.sql = text })
        sqlError = ""
    }
    function run() {
        if (!applySql()) { window.notify(qsTr("The SQL does not fit the grid"), sqlError, "warning"); return }
        if (!dirty && name.length > 0) window.commands.runQuery(name, ({}), ({}))
        else window.commands.runQuery(name, runnable(), ({}))
    }
    function save() {
        if (!applySql()) { window.notify(qsTr("Query not saved"), sqlError, "danger"); return false }
        let target = name
        if (target.length === 0) {
            let n = base.queryNames.length + 1
            while (base.queryNames.indexOf(qsTr("Query%1").arg(n)) >= 0) ++n
            target = qsTr("Query%1").arg(n)
        }
        const r = base.saveQuery(target, runnable())
        if (r.needsSystemTables) {
            window.confirmDialog.ask(qsTr("Add QindaBase's tables?"), r.error, "question", qsTr("Add them"), function() {
                base.addSystemTables(); if (save()) window.baseHost.designDirty = false
            })
            return false
        }
        if (!r.ok) { window.notify(qsTr("Query not saved"), r.error, "danger"); return false }
        name = r.name
        original = JSON.stringify(design)
        window.baseHost.currentName = r.name
        window.baseHost.selectedKind = "query"
        window.baseHost.selectedName = r.name
        syncHost()
        return true
    }
    function discard() { load(original === "{}" ? base.newQueryDesign([]) : Object.assign(JSON.parse(original), { name: name })) }
    // A crosstab starts from the grid: the first column down, the second
    // across, the third summed in the cells (Access's Crosstab Wizard).
    function makeCrosstab(d) {
        d.kind = "crosstab"
        if (d.columns.some(function(c) { return c.crosstab })) return
        const roles = ["row", "column", "value"]
        for (let i = 0; i < d.columns.length && i < 3; ++i) {
            d.columns[i].crosstab = roles[i]
            if (roles[i] === "value" && ["sum", "average", "count", "min", "max"].indexOf(d.columns[i].total) < 0)
                d.columns[i].total = "sum"
        }
    }
    function command(id) {
        switch (id) {
        case "baseQueryRunAction": run(); break
        case "baseQueryAddTableAction": tablesPane.chooseTable(); break
        case "baseQueryTotalsAction": edit(function(d) { d.totals = !d.totals }); break
        case "baseQueryClearAction": edit(function(d) { d.columns = [] }); break
        case "baseQueryParametersAction": parametersDialog.edit(design.parameters || []); break
        case "baseQueryTypeSelectAction": edit(function(d) { d.kind = "select" }); mode = "design"; break
        case "baseQueryTypeUpdateAction": edit(function(d) { d.kind = "update" }); mode = "design"; break
        case "baseQueryTypeDeleteAction": edit(function(d) { d.kind = "delete" }); mode = "design"; break
        case "baseQueryTypeCrosstabAction": edit(makeCrosstab); mode = "design"; break
        case "baseQueryTypeMakeTableAction":
            edit(function(d) { d.kind = "makeTable"; if (!d.makeTable) d.makeTable = (view.name || qsTr("Query")) + qsTr(" table") })
            mode = "design"
            break
        case "baseQueryTypeAppendAction":
            edit(function(d) { d.kind = "append"; if (!d.appendTable) d.appendTable = view.base.tableNames[0] || "" })
            mode = "design"
            break
        }
    }

    QueryParametersDialog {
        id: parametersDialog
        objectName: "queryParametersDialog"
        types: view.base !== null ? view.base.parameterTypes : []
        onDeclared: function(list) { view.edit(function(d) { d.parameters = list }) }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        QueryDesignHeader {
            Tk.Flex.shrink: 0
            view: view
        }
        Tk.Scroll {
            id: body
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            accessibleName: qsTr("Query design")
            Tk.Flex {
                width: body.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                padding: Tk.Theme.space.md
                QueryTablesPane {
                    id: tablesPane
                    visible: view.mode === "design"
                    view: view
                }
                QueryGrid {
                    visible: view.mode === "design"
                    width: body.width - Tk.Theme.space.md * 2
                    view: view
                }
                QuerySqlPane {
                    width: body.width - Tk.Theme.space.md * 2
                    view: view
                }
            }
        }
    }
}
