// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One column of the query design grid, top to bottom: field (or an
// expression), output name, total, crosstab role (M80), sort, show,
// criteria, or — and "Update to" / "Append to" for action queries. Rows
// line up with the grid's label column (QueryDesignView.rowLabels) through
// the shared `rowHeight`; the two must show the same rows.
//
// AGENT-GUARD: a row's visibility here and its label in rowLabels use the
// same conditions; one without the other shifts every row below it.
Tk.Box {
    id: column

    property var spec: ({})
    property int columnIndex: 0
    property var fieldChoices: []   // ["donations.*", "donations.amount", …]
    property var appendFields: []
    property bool totals: false
    property string kind: "select"
    property real rowHeight: Tk.Theme.size.control
    // An expression column ("" field); " " marks one not typed yet.
    readonly property bool expression: (spec.expression || "").length > 0
    readonly property string fieldKey: spec.field ? ((spec.table ? spec.table + "." : "") + spec.field) : ""
    readonly property var totalIds: ["groupBy", "sum", "average", "count", "min", "max", "where", "expression"]
    readonly property var crosstabIds: ["row", "column", "value", ""]
    readonly property bool selectLike: kind === "select" || kind === "makeTable"
    readonly property bool crosstab: kind === "crosstab"
    signal edited(var patch)
    signal removed()

    width: Tk.Theme.size.fieldWidth * 0.9
    color: Tk.Theme.color.panelAlt
    borderLeft: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider
    padding: Tk.Theme.space.xs

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Item {
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            Tk.ComboBox {
                objectName: "queryField_" + column.columnIndex
                anchors.fill: parent
                visible: !column.expression
                small: true
                model: [qsTr("(Expression)")].concat(column.fieldChoices)
                currentIndex: Math.max(0, model.indexOf(column.fieldKey))
                tooltip: qsTr("Field")
                onActivated: function(i) {
                    if (i === 0) { column.edited({ expression: column.spec.field ? "[" + column.spec.field + "]" : " " }); return }
                    const parts = String(model[i]).split(".")
                    column.edited({ table: parts[0], field: parts.slice(1).join("."), expression: "" })
                }
            }
            Tk.TextField {
                objectName: "queryExpression_" + column.columnIndex
                anchors.fill: parent
                visible: column.expression
                small: true
                mono: true
                text: (column.spec.expression || "").trim()
                placeholderText: "[amount] * 2"
                tooltip: qsTr("Expression, e.g. Year([given]) or [qty] * [price]")
                onEditingFinished: if (text !== (column.spec.expression || "").trim()) column.edited({ expression: text.length ? text : " " })
            }
        }
        Tk.TextField {
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            text: column.spec.alias || ""
            placeholderText: column.expression ? qsTr("Expr%1").arg(column.columnIndex + 1) : (column.spec.field || "")
            tooltip: qsTr("Name of the column in the results")
            onEditingFinished: if (text !== (column.spec.alias || "")) column.edited({ alias: text })
        }
        Tk.ComboBox {
            objectName: "queryTotal_" + column.columnIndex
            visible: (column.totals && column.selectLike) || column.crosstab
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            model: [qsTr("Group By"), qsTr("Sum"), qsTr("Avg"), qsTr("Count"), qsTr("Min"), qsTr("Max"), qsTr("Where"), qsTr("Expression")]
            currentIndex: Math.max(0, column.totalIds.indexOf(column.spec.total || "groupBy"))
            tooltip: qsTr("Total")
            onActivated: function(i) { column.edited({ total: column.totalIds[i] }) }
        }
        Tk.ComboBox {
            objectName: "queryCrosstab_" + column.columnIndex
            visible: column.crosstab
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            model: [qsTr("Row Heading"), qsTr("Column Heading"), qsTr("Value"), qsTr("(not shown)")]
            currentIndex: Math.max(0, column.crosstabIds.indexOf(column.spec.crosstab || ""))
            tooltip: qsTr("Crosstab: down the side, across the top, or the totals in the cells")
            onActivated: function(i) { column.edited({ crosstab: column.crosstabIds[i] }) }
        }
        Tk.ComboBox {
            visible: column.selectLike || column.crosstab
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            model: [qsTr("(not sorted)"), qsTr("Ascending"), qsTr("Descending")]
            currentIndex: column.spec.sort === 1 ? 1 : column.spec.sort === -1 ? 2 : 0
            tooltip: qsTr("Sort")
            onActivated: function(i) { column.edited({ sort: i === 1 ? 1 : i === 2 ? -1 : 0 }) }
        }
        Item {
            visible: column.selectLike
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            width: parent.width
            Tk.CheckBox {
                anchors.centerIn: parent
                checked: column.spec.show !== false
                tooltip: qsTr("Show in the results")
                onToggled: column.edited({ show: checked })
            }
        }
        Tk.TextField {
            visible: column.kind === "update"
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            mono: true
            text: column.spec.updateTo || ""
            placeholderText: qsTr("new value")
            tooltip: qsTr("Update to: a value or an expression like [amount] * 1.05")
            onEditingFinished: if (text !== (column.spec.updateTo || "")) column.edited({ updateTo: text })
        }
        Tk.ComboBox {
            visible: column.kind === "append"
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            model: [qsTr("(not appended)")].concat(column.appendFields)
            currentIndex: Math.max(0, model.indexOf(column.spec.appendTo || ""))
            tooltip: qsTr("Append to this field")
            onActivated: function(i) { column.edited({ appendTo: i === 0 ? "" : model[i] }) }
        }
        Tk.TextField {
            objectName: "queryCriteria_" + column.columnIndex
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            mono: true
            text: column.spec.criteria || ""
            tooltip: qsTr("Criteria: > 100, Between #1/1/2026# And #3/31/2026#, Like \"A*\", [Ask me]")
            onEditingFinished: if (text !== (column.spec.criteria || "")) column.edited({ criteria: text })
        }
        Tk.TextField {
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            small: true
            mono: true
            text: (column.spec.orCriteria || [])[0] || ""
            tooltip: qsTr("Or: rows match this instead")
            onEditingFinished: if (text !== ((column.spec.orCriteria || [])[0] || "")) column.edited({ orCriteria: text.length ? [text] : [] })
        }
        Tk.IconButton {
            Tk.Flex.basis: column.rowHeight
            Tk.Flex.shrink: 0
            iconName: "x"
            small: true
            tooltip: qsTr("Remove this column")
            onClicked: column.removed()
        }
    }
}
