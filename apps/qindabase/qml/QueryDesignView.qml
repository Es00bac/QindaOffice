// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The query designer: the tables and how they join, the design grid
// (Access's rows: field, name, total, sort, show, criteria, or — and update
// to / append to for action queries), and the SQL it compiles to, live.
// SQL view edits the statement directly. Running a draft needs no save;
// action queries always ask with the number of records they will change.
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
    readonly property bool dirty: JSON.stringify(design) !== original
    readonly property var compiled: base !== null && window.view === "queryDesign" ? base.compileQuery(runnable()) : ({})
    readonly property real rowHeight: Tk.Theme.size.control
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
        if (design.totals && design.kind === "select") out.push(qsTr("Total"))
        if (design.kind === "select") out.push(qsTr("Sort"), qsTr("Show"))
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
        syncHost()
    }
    function redo() {
        if (future.length === 0) return
        history = history.concat([JSON.stringify(design)])
        design = JSON.parse(future[future.length - 1])
        future = future.slice(0, -1)
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
    function addColumn(table, field) {
        edit(function(d) {
            d.columns.push({ table: table, field: field, expression: "", alias: "", total: "groupBy", sort: 0, show: true,
                             criteria: "", orCriteria: [], updateTo: "", appendTo: "" })
        })
    }
    function showSql() {
        if (mode === "sql") return
        const sql = compiled.ok ? compiled.sql : ""
        edit(function(d) { d.kind = "sql"; d.sql = sql })
        mode = "sql"
    }
    function showDesign() {
        if (mode === "design") return
        if (design.kind === "sql" && original.indexOf("\"kind\":\"sql\"") >= 0 && (design.tables || []).length === 0) {
            window.notify(qsTr("SQL view only"), qsTr("This query was written in SQL; it has no design grid."), "info")
            return
        }
        edit(function(d) { d.kind = "select" })
        mode = "design"
    }
    function run() {
        if (!dirty && name.length > 0) window.commands.runQuery(name, ({}), ({}))
        else window.commands.runQuery(name, runnable(), ({}))
    }
    function save() {
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
    function command(id) {
        switch (id) {
        case "baseQueryRunAction": run(); break
        case "baseQueryAddTableAction": addMenu.popup(header, 0, header.height); break
        case "baseQueryTotalsAction": edit(function(d) { d.totals = !d.totals }); break
        case "baseQueryClearAction": edit(function(d) { d.columns = [] }); break
        case "baseQueryTypeSelectAction": edit(function(d) { d.kind = "select" }); mode = "design"; break
        case "baseQueryTypeUpdateAction": edit(function(d) { d.kind = "update" }); mode = "design"; break
        case "baseQueryTypeDeleteAction": edit(function(d) { d.kind = "delete" }); mode = "design"; break
        case "baseQueryTypeAppendAction":
            edit(function(d) { d.kind = "append"; if (!d.appendTable) d.appendTable = view.base.tableNames[0] || "" })
            mode = "design"
            break
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            id: header
            objectName: "queryDesignHeader"
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
                Tk.Icon { name: "funnel"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                Tk.Heading { objectName: "queryDesignName"; level: 3; text: view.name || qsTr("New query"); Tk.Flex.shrink: 0 }
                Tk.Badge {
                    text: ({ select: qsTr("select"), update: qsTr("update"), delete: qsTr("delete"), append: qsTr("append"), sql: qsTr("SQL") })[view.design.kind] || ""
                    variant: view.design.kind === "select" || view.design.kind === "sql" ? "accent" : "warning"
                }
                Tk.ComboBox {
                    visible: view.design.kind === "append"
                    small: true
                    model: view.base !== null ? view.base.tableNames : []
                    currentIndex: model.indexOf(view.design.appendTable || "")
                    tooltip: qsTr("Append to table")
                    onActivated: function(i) { view.edit(function(d) { d.appendTable = model[i] }) }
                }
                Tk.Spacer { }
                Tk.Segmented {
                    objectName: "queryMode"
                    small: true
                    model: [{ text: qsTr("Design"), iconName: "table-2" }, { text: qsTr("SQL"), iconName: "code" }]
                    currentIndex: view.mode === "sql" ? 1 : 0
                    onActivated: function(i) { if (i === 1) view.showSql(); else view.showDesign() }
                }
                Tk.Button { text: qsTr("Save"); iconName: "save"; enabled: view.dirty || view.name.length === 0; onClicked: view.save() }
                Tk.Button { objectName: "queryRun"; text: qsTr("Run"); iconName: "play"; variant: "accent"; onClicked: view.run() }
            }
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
                // ---- tables and joins ----
                Tk.Flex {
                    visible: view.mode === "design"
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.md
                    Repeater {
                        model: view.design.tables || []
                        delegate: Tk.Card {
                            id: tableCard
                            required property var modelData
                            objectName: "queryTable_" + modelData.name
                            width: Tk.Theme.size.fieldWidth
                            Tk.Flex.basis: Tk.Theme.size.fieldWidth
                            Tk.Flex.shrink: 0
                            padding: 0
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: 0
                                Tk.Box {
                                    color: Tk.Theme.color.headerBg
                                    padding: Tk.Theme.space.xs
                                    paddingLeft: Tk.Theme.space.sm
                                    Tk.Flex {
                                        direction: Tk.Flex.Row
                                        align: Tk.Flex.Center
                                        Tk.Label { text: tableCard.modelData.name; font.weight: Font.DemiBold; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                        Tk.IconButton { iconName: "x"; small: true; tooltip: qsTr("Remove %1").arg(tableCard.modelData.name); onClicked: view.removeTable(tableCard.modelData.name) }
                                    }
                                }
                                Repeater {
                                    model: ["*"].concat(view.base.tableFields(tableCard.modelData.name).map(function(f) { return f.name }))
                                    delegate: Tk.ListRow {
                                        required property string modelData
                                        small: true
                                        text: modelData === "*" ? qsTr("All fields") : modelData
                                        iconName: modelData === "*" ? "asterisk" : ""
                                        tooltip: qsTr("Add %1 to the grid").arg(modelData)
                                        onClicked: view.addColumn(tableCard.modelData.name, modelData)
                                    }
                                }
                            }
                        }
                    }
                    Tk.Button {
                        objectName: "queryAddTable"
                        text: qsTr("Add table")
                        iconName: "plus"
                        onClicked: addMenu.popup(this, 0, height)
                    }
                }
                Tk.Flex {
                    visible: view.mode === "design" && (view.design.joins || []).length > 0
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.Overline { title: qsTr("JOINS") }
                    Repeater {
                        model: view.design.joins || []
                        delegate: Tk.Box {
                            id: joinBox
                            required property var modelData
                            required property int index
                            objectName: "queryJoin_" + index
                            color: Tk.Theme.color.chipBg
                            radius: Tk.Theme.radius.md
                            padding: Tk.Theme.space.xs
                            paddingLeft: Tk.Theme.space.sm
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Icon { name: "link"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.accent }
                                Tk.Label {
                                    mono: true
                                    text: joinBox.modelData.leftTable + "." + joinBox.modelData.leftField + " = "
                                          + joinBox.modelData.rightTable + "." + joinBox.modelData.rightField
                                    Tk.Flex.shrink: 0
                                }
                                Tk.ComboBox {
                                    small: true
                                    model: [qsTr("matching rows"), qsTr("all %1").arg(joinBox.modelData.leftTable), qsTr("all %1").arg(joinBox.modelData.rightTable)]
                                    currentIndex: joinBox.modelData.kind === "left" ? 1 : joinBox.modelData.kind === "right" ? 2 : 0
                                    tooltip: qsTr("Which rows the join keeps")
                                    onActivated: function(i) { const at = joinBox.index; view.edit(function(d) { d.joins[at].kind = ["inner", "left", "right"][i] }) }
                                }
                                Tk.IconButton { iconName: "x"; small: true; tooltip: qsTr("Remove join"); onClicked: { const at = joinBox.index; view.edit(function(d) { d.joins.splice(at, 1) }) } }
                            }
                        }
                    }
                }
                // ---- the design grid ----
                Tk.Scroll {
                    id: gridScroll
                    visible: view.mode === "design"
                    width: body.width - Tk.Theme.space.md * 2
                    implicitHeight: gridRow.implicitHeight + Tk.Theme.size.scrollbar
                    // Columns keep their width and the grid scrolls sideways
                    // in a narrow window (Access's grid does the same).
                    overflowX: Tk.Scroll.Auto
                    overflowY: Tk.Scroll.Hidden
                    accessibleName: qsTr("Design grid")
                    Tk.Flex {
                        id: gridRow
                        objectName: "queryGrid"
                        direction: Tk.Flex.Row
                        gap: 0
                        Tk.Box {
                            color: Tk.Theme.color.headerBg
                            padding: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: Tk.Theme.space.xs
                                Repeater {
                                    model: view.rowLabels
                                    delegate: Tk.Caption {
                                        required property string modelData
                                        text: modelData
                                        verticalAlignment: Text.AlignVCenter
                                        Tk.Flex.basis: view.rowHeight
                                        Tk.Flex.shrink: 0
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: view.design.columns || []
                            delegate: QueryGridColumn {
                                required property var modelData
                                required property int index
                                spec: modelData
                                columnIndex: index
                                fieldChoices: view.fieldChoices
                                appendFields: view.design.kind === "append" && view.design.appendTable
                                              ? view.base.tableFields(view.design.appendTable).map(function(f) { return f.name }) : []
                                totals: view.design.totals === true
                                kind: view.design.kind
                                rowHeight: view.rowHeight
                                onEdited: function(patch) { const at = index; view.edit(function(d) { Object.assign(d.columns[at], patch) }) }
                                onRemoved: { const at = index; view.edit(function(d) { d.columns.splice(at, 1) }) }
                            }
                        }
                        Tk.Box {
                            color: Tk.Theme.color.panelAlt
                            borderLeft: Tk.Theme.size.border
                            borderColor: Tk.Theme.color.divider
                            padding: Tk.Theme.space.sm
                            Tk.Button {
                                objectName: "queryAddColumn"
                                text: qsTr("Add column")
                                iconName: "plus"
                                small: true
                                onClicked: view.edit(function(d) {
                                    d.columns.push({ table: "", field: "", expression: " ", alias: "", total: "groupBy", sort: 0, show: true,
                                                     criteria: "", orCriteria: [], updateTo: "", appendTo: "" })
                                })
                            }
                        }
                    }
                }
                // ---- SQL ----
                Tk.Notice {
                    objectName: "queryProblem"
                    visible: view.compiled.ok === false && !!view.compiled.error && (view.design.tables || []).length + (view.design.sql || "").length > 0
                    variant: "warning"
                    text: view.compiled.error || ""
                }
                Tk.Flex {
                    visible: (view.compiled.parameters || []).length > 0
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.Overline { title: qsTr("ASKS FOR") }
                    Repeater {
                        model: view.compiled.parameters || []
                        delegate: Tk.Chip { required property var modelData; small: true; iconName: "circle-question-mark"; text: modelData.prompt + " · " + modelData.typeLabel }
                    }
                }
                Tk.Overline { title: view.mode === "sql" ? qsTr("SQL — ONE STATEMENT; :name ASKS FOR A VALUE") : qsTr("SQL") }
                Tk.TextArea {
                    objectName: "querySql"
                    mono: true
                    rows: view.mode === "sql" ? 10 : 5
                    readOnly: view.mode !== "sql"
                    wrapMode: TextEdit.Wrap
                    text: view.mode === "sql" ? (view.design.sql || "") : (view.compiled.sql || "")
                    tooltip: qsTr("The SQL this query runs")
                    onEditingFinished: if (view.mode === "sql" && text !== view.design.sql) view.edit(function(d) { d.sql = text })
                }
            }
        }
    }

    Tk.Menu {
        id: addMenu
        Repeater {
            model: view.base !== null ? view.base.tableNames : []
            delegate: Tk.MenuItem {
                required property string modelData
                text: modelData
                iconName: "table"
                onTriggered: view.addTable(modelData)
            }
        }
    }
}
