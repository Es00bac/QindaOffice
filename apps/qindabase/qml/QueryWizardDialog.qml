// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Access's query wizards (M86, D-094): Simple Query (fields of related
// tables, detail or summary), Crosstab Query (row headings, a column
// heading — a date by year, quarter or month — and a total), Find
// Duplicates and Find Unmatched. The engine writes the design
// (BaseService.queryWizard, base_query_wizards.h); Finish saves it under its
// name as one undo step and opens it, or its design.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string kind: "simple" // simple | crosstab | duplicates | unmatched
    property string table: ""
    property var fields: []          // the chosen table's fields
    property var picked: []          // simple: "table.field", in order
    property var totals: ({})        // simple: "table.field" → sum | average | min | max
    property bool summary: false
    property bool countRecords: true
    property string dateGrouping: "unique"
    property var rows: ["", "", ""]  // crosstab: up to three row headings
    property string across: ""
    property string interval: "year"
    property string valueField: ""
    property string valueFunction: "sum"
    property bool rowSums: true
    property var chosen: []          // duplicates: repeated fields; unmatched: shown fields
    property var others: []          // duplicates: the other fields shown
    property string related: ""      // unmatched
    property string field: ""
    property string relatedField: ""
    property bool modify: false
    property string error: ""
    readonly property var tables: base !== null ? base.tableNames : []
    readonly property var names: fields.map(function(f) { return f.name })
    readonly property var functions: [{ id: "sum", label: qsTr("Sum") }, { id: "average", label: qsTr("Avg") },
                                      { id: "count", label: qsTr("Count") }, { id: "min", label: qsTr("Min") },
                                      { id: "max", label: qsTr("Max") }]

    title: ({ simple: qsTr("Simple Query Wizard"), crosstab: qsTr("Crosstab Query Wizard"),
              duplicates: qsTr("Find Duplicates Query Wizard"), unmatched: qsTr("Find Unmatched Query Wizard") })[kind]
    subtitle: ({ simple: qsTr("Fields from one table or from related tables, every record or totals."),
                 crosstab: qsTr("A summary with one field down the side and one across the top."),
                 duplicates: qsTr("The records whose values repeat in the fields you choose."),
                 unmatched: qsTr("The records of one table with no related record in another.") })[kind]
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Finish")
    secondaryText: qsTr("Cancel")
    primaryEnabled: nameField.text.trim().length > 0 && table.length > 0

    function fieldsOf(t) { return t ? base.tableFields(t) : [] }
    function typeOf(name) { const f = fields.find(function(x) { return x.name === name }); return f ? f.type : "" }
    function openFor(wizard, preferred) {
        kind = wizard
        error = ""
        picked = []
        totals = ({})
        summary = false
        modify = false
        choose(tables.indexOf(preferred) >= 0 ? preferred : (tables[0] || ""))
        open()
    }
    function choose(t) {
        table = t
        fields = fieldsOf(t)
        const plain = names.filter(function(n) { return n !== "id" })
        // First answers as Access gives them: text down the side, a date
        // across, money (else a number that is no key or lookup) to total.
        const firstOf = function(types) {
            const f = fields.find(function(x) { return types.indexOf(x.type) >= 0 && !x.primaryKey && !x.lookupTable })
            return f ? f.name : ""
        }
        rows = [firstOf(["text"]) || plain[0] || "", "", ""]
        across = firstOf(["date", "dateTime"]) || plain[1] || ""
        const value = firstOf(["currency"]) || firstOf(["number", "integer", "largeNumber"])
        valueField = value || plain[2] || ""
        valueFunction = value ? "sum" : "count"
        chosen = kind === "unmatched" ? names.slice(0, 3) : (plain.length ? [plain[0]] : [])
        others = []
        related = tables.find(function(x) { return x !== t }) || ""
        matchFields()
        const label = t.charAt(0).toUpperCase() + t.slice(1)
        nameField.text = ({ simple: qsTr("%1 Query").arg(label), crosstab: qsTr("%1_Crosstab").arg(label),
                            duplicates: qsTr("Find duplicates for %1").arg(label),
                            unmatched: qsTr("%1 Without Matching %2").arg(label).arg(related) })[kind]
    }
    // The fields the two tables have in common: their relationship, else a key.
    function matchFields() {
        const rel = base.relationships().find(function(r) {
            return (r.childTable === related && r.parentTable === table) || (r.childTable === table && r.parentTable === related)
        })
        if (rel) {
            field = rel.childTable === table ? rel.childField : rel.parentField
            relatedField = rel.childTable === table ? rel.parentField : rel.childField
        } else {
            const key = fields.find(function(f) { return f.primaryKey })
            field = key ? key.name : (names[0] || "")
            relatedField = fieldsOf(related).length ? fieldsOf(related)[0].name : ""
        }
    }
    function toggle(list, name, on) {
        const next = list.filter(function(n) { return n !== name })
        if (on) next.push(name)
        return next
    }
    function spec() {
        switch (kind) {
        case "simple":
            return { fields: picked, summary: summary, countRecords: countRecords, dateGrouping: dateGrouping,
                     totals: Object.keys(totals).filter(function(k) { return totals[k] && picked.indexOf(k) >= 0 })
                                 .map(function(k) { return { field: k, function: totals[k] } }) }
        case "crosstab":
            return { table: table, rowHeadings: rows.filter(function(r) { return r }), columnHeading: across,
                     dateInterval: interval, valueField: valueField, function: valueFunction, rowSums: rowSums }
        case "duplicates":
            return { table: table, duplicateFields: chosen, otherFields: others }
        }
        return { table: table, relatedTable: related, field: field, relatedField: relatedField, showFields: chosen }
    }
    onAccepted: {
        const made = base.queryWizard(kind, spec())
        if (!made.ok) { window.notify(qsTr("No query made"), made.error, "danger"); Qt.callLater(open); error = made.error; return }
        const saved = base.saveQuery(nameField.text.trim(), made.design)
        if (!saved.ok) { window.notify(qsTr("Query not saved"), saved.error, "danger"); return }
        window.baseHost.selectedKind = "query"
        window.baseHost.selectedName = saved.name
        if (modify) window.commands.designObject("query", saved.name)
        else window.commands.openObject("query", saved.name)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: dialog.kind === "unmatched" ? qsTr("Records of") : qsTr("Table")
            Tk.ComboBox {
                objectName: "queryWizardTable"
                small: true
                model: dialog.tables
                currentIndex: model.indexOf(dialog.table)
                onActivated: function(i) { dialog.choose(model[i]) }
            }
        }
        // ---- Simple Query ----
        Tk.Overline { visible: dialog.kind === "simple"; title: qsTr("FIELDS (PICK FROM ANY RELATED TABLE)") }
        Tk.Grid {
            visible: dialog.kind === "simple"
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.md
            Repeater {
                model: dialog.kind === "simple" ? dialog.fields : []
                delegate: Tk.CheckBox {
                    required property var modelData
                    readonly property string key: dialog.table + "." + modelData.name
                    objectName: "queryWizardField_" + modelData.name
                    text: modelData.caption || modelData.name
                    checked: dialog.picked.indexOf(key) >= 0
                    onToggled: dialog.picked = dialog.toggle(dialog.picked, key, checked)
                }
            }
        }
        Tk.Flex {
            visible: dialog.kind === "simple" && dialog.picked.length > 0
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: dialog.kind === "simple" ? dialog.picked : []
                delegate: Tk.Chip {
                    required property string modelData
                    small: true
                    text: modelData
                    tooltip: qsTr("Leave this field out")
                    onClicked: dialog.picked = dialog.toggle(dialog.picked, modelData, false)
                }
            }
        }
        Tk.CheckBox {
            objectName: "queryWizardSummary"
            visible: dialog.kind === "simple"
            text: qsTr("Summary: group the records and total their numbers")
            checked: dialog.summary
            onToggled: dialog.summary = checked
        }
        Repeater {
            model: dialog.kind === "simple" && dialog.summary ? dialog.picked : []
            delegate: Tk.PropertyRow {
                id: totalRow
                required property string modelData
                readonly property var info: dialog.fieldsOf(modelData.split(".")[0]).find(function(f) { return f.name === modelData.split(".")[1] }) || ({})
                visible: ["integer", "number", "currency", "largeNumber"].indexOf(info.type) >= 0
                label: modelData
                Tk.ComboBox {
                    small: true
                    model: [qsTr("Group by it"), qsTr("Sum"), qsTr("Avg"), qsTr("Min"), qsTr("Max")]
                    currentIndex: Math.max(0, ["", "sum", "average", "min", "max"].indexOf(dialog.totals[totalRow.modelData] || ""))
                    onActivated: function(i) {
                        const next = Object.assign({}, dialog.totals)
                        next[totalRow.modelData] = ["", "sum", "average", "min", "max"][i]
                        dialog.totals = next
                    }
                }
            }
        }
        Tk.Flex {
            visible: dialog.kind === "simple" && dialog.summary
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.md
            align: Tk.Flex.Center
            Tk.CheckBox { text: qsTr("Count the records"); checked: dialog.countRecords; onToggled: dialog.countRecords = checked }
            Tk.Caption { text: qsTr("Group dates by") }
            Tk.Segmented {
                objectName: "queryWizardDates"
                small: true
                model: [qsTr("Value"), qsTr("Day"), qsTr("Month"), qsTr("Quarter"), qsTr("Year")]
                currentIndex: ["unique", "day", "month", "quarter", "year"].indexOf(dialog.dateGrouping)
                onActivated: function(i) { dialog.dateGrouping = ["unique", "day", "month", "quarter", "year"][i] }
            }
        }
        // ---- Crosstab Query ----
        Repeater {
            model: dialog.kind === "crosstab" ? 3 : 0
            delegate: Tk.PropertyRow {
                id: rowHeading
                required property int index
                label: index === 0 ? qsTr("Row headings") : ""
                Tk.ComboBox {
                    objectName: "queryWizardRow_" + rowHeading.index
                    small: true
                    model: [rowHeading.index === 0 ? qsTr("(choose)") : qsTr("(none)")].concat(dialog.names)
                    currentIndex: Math.max(0, model.indexOf(dialog.rows[rowHeading.index]))
                    onActivated: function(i) { const next = dialog.rows.slice(); next[rowHeading.index] = i === 0 ? "" : model[i]; dialog.rows = next }
                }
            }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "crosstab"
            label: qsTr("Column heading")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox {
                    objectName: "queryWizardAcross"
                    small: true
                    model: dialog.names
                    currentIndex: model.indexOf(dialog.across)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.across = model[i] }
                }
                Tk.ComboBox {
                    objectName: "queryWizardInterval"
                    visible: ["date", "dateTime"].indexOf(dialog.typeOf(dialog.across)) >= 0
                    small: true
                    textRole: "label"
                    model: [{ id: "year", label: qsTr("by year") }, { id: "quarter", label: qsTr("by quarter") },
                            { id: "month", label: qsTr("by month") }, { id: "date", label: qsTr("by date") },
                            { id: "dateTime", label: qsTr("as it is") }]
                    currentIndex: Math.max(0, model.findIndex(function(m) { return m.id === dialog.interval }))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.interval = model[i].id }
                }
            }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "crosstab"
            label: qsTr("Value")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox {
                    objectName: "queryWizardValue"
                    small: true
                    model: dialog.names
                    currentIndex: model.indexOf(dialog.valueField)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.valueField = model[i] }
                }
                Tk.ComboBox {
                    small: true
                    textRole: "label"
                    model: dialog.functions
                    currentIndex: Math.max(0, model.findIndex(function(m) { return m.id === dialog.valueFunction }))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.valueFunction = model[i].id }
                }
            }
        }
        Tk.CheckBox {
            visible: dialog.kind === "crosstab"
            text: qsTr("Include row sums")
            checked: dialog.rowSums
            onToggled: dialog.rowSums = checked
        }
        // ---- Find Duplicates / Find Unmatched ----
        Tk.PropertyRow {
            visible: dialog.kind === "unmatched"
            label: qsTr("With no match in")
            Tk.ComboBox {
                objectName: "queryWizardRelated"
                small: true
                model: dialog.tables.filter(function(t) { return t !== dialog.table })
                currentIndex: model.indexOf(dialog.related)
                onActivated: function(i) { dialog.related = model[i]; dialog.matchFields() }
            }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "unmatched"
            label: qsTr("Matching fields")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.ComboBox {
                    small: true
                    model: dialog.names
                    currentIndex: model.indexOf(dialog.field)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.field = model[i] }
                }
                Tk.Label { text: "⇄" }
                Tk.ComboBox {
                    small: true
                    model: dialog.fieldsOf(dialog.related).map(function(f) { return f.name })
                    currentIndex: model.indexOf(dialog.relatedField)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.relatedField = model[i] }
                }
            }
        }
        Tk.Overline {
            visible: dialog.kind === "duplicates" || dialog.kind === "unmatched"
            title: dialog.kind === "duplicates" ? qsTr("FIELDS WHOSE VALUES REPEAT") : qsTr("FIELDS TO SHOW")
        }
        Tk.Grid {
            visible: dialog.kind === "duplicates" || dialog.kind === "unmatched"
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.md
            Repeater {
                model: dialog.kind === "duplicates" || dialog.kind === "unmatched" ? dialog.fields : []
                delegate: Tk.CheckBox {
                    required property var modelData
                    objectName: "queryWizardPick_" + modelData.name
                    text: modelData.caption || modelData.name
                    checked: dialog.chosen.indexOf(modelData.name) >= 0
                    onToggled: dialog.chosen = dialog.toggle(dialog.chosen, modelData.name, checked)
                }
            }
        }
        Tk.Overline { visible: dialog.kind === "duplicates"; title: qsTr("OTHER FIELDS TO SHOW (NONE: COUNT THE REPEATS)") }
        Tk.Grid {
            visible: dialog.kind === "duplicates"
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.md
            Repeater {
                model: dialog.kind === "duplicates" ? dialog.fields.filter(function(f) { return dialog.chosen.indexOf(f.name) < 0 }) : []
                delegate: Tk.CheckBox {
                    required property var modelData
                    text: modelData.caption || modelData.name
                    checked: dialog.others.indexOf(modelData.name) >= 0
                    onToggled: dialog.others = dialog.toggle(dialog.others, modelData.name, checked)
                }
            }
        }
        Tk.Divider { }
        Tk.PropertyRow { label: qsTr("Query name"); Tk.TextField { id: nameField; objectName: "queryWizardName"; small: true } }
        Tk.CheckBox {
            objectName: "queryWizardModify"
            text: qsTr("Open the design instead of the results")
            checked: dialog.modify
            onToggled: dialog.modify = checked
        }
    }
}
