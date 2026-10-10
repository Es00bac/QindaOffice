// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The query designer's header: the query's name and type, what the type
// needs (the table an append query fills, the table a make-table query
// makes, a crosstab's fixed column headings), Access's Return box (Top
// values, also as a percentage) with Unique Values, Design/SQL, Save, Run.
// It wraps in a narrow window (M80, D-089 §3).
Tk.Box {
    id: header

    required property var view
    readonly property var design: view.design
    readonly property var topChoices: [
        { text: qsTr("All"), top: 0, percent: false }, { text: "5", top: 5, percent: false },
        { text: "25", top: 25, percent: false }, { text: "100", top: 100, percent: false },
        { text: "5%", top: 5, percent: true }, { text: "25%", top: 25, percent: true }]

    function topIndex() {
        for (let i = 0; i < topChoices.length; ++i) {
            if (topChoices[i].top === (design.top || 0) && topChoices[i].percent === (design.topPercent === true)) return i
        }
        return -1
    }

    objectName: "queryDesignHeader"
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
        Tk.Heading { objectName: "queryDesignName"; level: 3; text: header.view.name || qsTr("New query"); Tk.Flex.shrink: 0 }
        Tk.Badge {
            objectName: "queryKindBadge"
            text: ({ select: qsTr("select"), update: qsTr("update"), delete: qsTr("delete"), append: qsTr("append"),
                     crosstab: qsTr("crosstab"), makeTable: qsTr("make table"), sql: qsTr("SQL"),
                     union: qsTr("union") })[header.design.kind] || ""
            variant: ["select", "sql", "crosstab", "union"].indexOf(header.design.kind) >= 0 ? "accent" : "warning"
        }
        Tk.ComboBox {
            objectName: "queryAppendTable"
            visible: header.design.kind === "append"
            small: true
            model: header.view.base !== null ? header.view.base.tableNames : []
            currentIndex: model.indexOf(header.design.appendTable || "")
            tooltip: qsTr("Append to table")
            onActivated: function(i) { const t = model[i]; header.view.edit(function(d) { d.appendTable = t }) }
        }
        Tk.TextField {
            objectName: "queryMakeTable"
            visible: header.design.kind === "makeTable"
            small: true
            text: header.design.makeTable || ""
            placeholderText: qsTr("New table name")
            tooltip: qsTr("The table this query makes; running it again replaces it (a backup is kept)")
            Tk.Flex.basis: Tk.Theme.size.fieldWidth
            onEditingFinished: { const t = text.trim(); if (t !== (header.design.makeTable || "")) header.view.edit(function(d) { d.makeTable = t }) }
        }
        Tk.TextField {
            objectName: "queryColumnHeadings"
            visible: header.design.kind === "crosstab"
            small: true
            text: (header.design.columnHeadings || []).join(", ")
            placeholderText: qsTr("Fixed column headings")
            tooltip: qsTr("Fixed column headings, in order, separated by commas (empty: every value the records hold)")
            Tk.Flex.basis: Tk.Theme.size.fieldWidth * 1.3
            onEditingFinished: {
                const list = text.split(",").map(function(h) { return h.trim() }).filter(function(h) { return h.length > 0 })
                if (JSON.stringify(list) !== JSON.stringify(header.design.columnHeadings || []))
                    header.view.edit(function(d) { d.columnHeadings = list })
            }
        }
        Tk.Flex {
            visible: header.view.selectLike
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Return"); Tk.Flex.shrink: 0 }
            Tk.ComboBox {
                objectName: "queryTop"
                small: true
                textRole: "text"
                model: header.topChoices
                currentIndex: header.topIndex()
                displayText: currentIndex >= 0 ? header.topChoices[currentIndex].text
                                               : (header.design.top || 0) + (header.design.topPercent ? "%" : "")
                tooltip: qsTr("Top values: how many records (or what share of them) the query returns")
                onActivated: function(i) {
                    const c = header.topChoices[i]
                    header.view.edit(function(d) { d.top = c.top; d.topPercent = c.percent })
                }
            }
            Tk.CheckBox {
                objectName: "queryUnique"
                text: qsTr("Unique values")
                checked: header.design.distinct === true
                tooltip: qsTr("Leave out records that repeat another one in every shown column")
                onToggled: { const on = checked; header.view.edit(function(d) { d.distinct = on }) }
            }
        }
        Tk.Spacer { }
        Tk.Segmented {
            objectName: "queryMode"
            small: true
            model: [{ text: qsTr("Design"), iconName: "table-2" }, { text: qsTr("SQL"), iconName: "code" }]
            currentIndex: header.view.mode === "sql" ? 1 : 0
            onActivated: function(i) { if (i === 1) header.view.showSql(); else header.view.showDesign() }
        }
        Tk.Button {
            text: qsTr("Save"); iconName: "save"
            enabled: header.view.dirty || header.view.name.length === 0
            onClicked: header.view.save()
        }
        Tk.Button { objectName: "queryRun"; text: qsTr("Run"); iconName: "play"; variant: "accent"; onClicked: header.view.run() }
    }
}
