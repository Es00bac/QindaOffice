// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One macro action and its arguments (M80, D-089 §5) — a row of the macro
// designer, and the one action a form's command button runs. The action
// list and its arguments are the engine's catalog (base_macro.h); object
// arguments pick from the database's forms, reports, queries, tables and
// macros, where conditions are Access expressions ([city] = "Portland").
Tk.Card {
    id: row

    property var base: null
    property var step: ({ action: "openForm", arguments: ({}) })
    property int stepIndex: 0
    property bool compact: false // the button dialog: one action, no ordering
    property bool canMoveUp: false
    property bool canMoveDown: false
    signal edited(var step)
    signal removed()
    signal moved(int delta)
    signal picked()

    readonly property var actions: base !== null ? base.macroActions : []
    readonly property var info: actions.find(function(a) { return a.id === row.step.action }) || ({ arguments: [], description: "" })

    // A choice argument's words (the stored values are Access's ids).
    readonly property var choiceWords: ({ edit: qsTr("Edit"), add: qsTr("Add a record"), preview: qsTr("Print Preview"),
                                          print: qsTr("Print"), first: qsTr("First"), previous: qsTr("Previous"),
                                          next: qsTr("Next"), last: qsTr("Last"), "new": qsTr("New record") })
    // [{id, text}] for an argument's combo box, "(choose)" first.
    function optionsFor(argument) {
        const ids = argument.kind === "choice" ? argument.choices : namesFor(argument.kind)
        return [{ id: "", text: qsTr("(choose)") }].concat(ids.map(function(id) {
            return { id: id, text: argument.kind === "choice" ? (row.choiceWords[id] || id) : id }
        }))
    }
    function namesFor(kind) {
        if (base === null) return []
        return ({ form: base.formNames, report: base.reportNames, query: base.queryNames, table: base.tableNames,
                  macro: base.macroNames })[kind] || []
    }
    function setArgument(id, value) {
        const args = Object.assign({}, row.step.arguments || {})
        if (value === "" || value === null || value === undefined) delete args[id]
        else args[id] = value
        row.edited({ action: row.step.action, arguments: args })
    }

    objectName: "macroAction_" + stepIndex
    padding: Tk.Theme.space.sm
    interactive: !compact
    tooltip: info.label || ""
    onClicked: row.picked()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            wrap: Tk.Flex.Wrap
            Tk.Badge { visible: !row.compact; text: String(row.stepIndex + 1) }
            Tk.ComboBox {
                objectName: "macroActionKind_" + row.stepIndex
                small: true
                textRole: "label"
                model: row.actions
                currentIndex: Math.max(0, row.actions.findIndex(function(a) { return a.id === row.step.action }))
                tooltip: qsTr("Macro action")
                Tk.Flex.basis: Tk.Theme.size.fieldWidth
                onActivated: function(i) { row.picked(); row.edited({ action: row.actions[i].id, arguments: ({}) }) }
            }
            Tk.Caption { text: row.info.description || ""; wrapMode: Text.Wrap; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.IconButton {
                visible: !row.compact
                iconName: "arrow-up"; small: true; enabled: row.canMoveUp
                tooltip: qsTr("Move up")
                onClicked: row.moved(-1)
            }
            Tk.IconButton {
                visible: !row.compact
                iconName: "arrow-down"; small: true; enabled: row.canMoveDown
                tooltip: qsTr("Move down")
                onClicked: row.moved(1)
            }
            Tk.IconButton {
                visible: !row.compact
                iconName: "x"; small: true
                tooltip: qsTr("Delete this action")
                onClicked: row.removed()
            }
        }
        Repeater {
            model: row.info.arguments || []
            delegate: Tk.PropertyRow {
                id: argument
                required property var modelData
                readonly property string value: String((row.step.arguments || {})[modelData.id] || "")
                readonly property bool picksName: ["form", "report", "query", "table", "macro"].indexOf(modelData.kind) >= 0
                label: modelData.label + (modelData.required ? " *" : "")
                hint: modelData.kind === "where" ? qsTr("An Access condition, e.g. [city] = \"Portland\"") : ""
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: 0
                    Tk.ComboBox {
                        id: picker
                        readonly property var options: argument.picksName || argument.modelData.kind === "choice"
                                                        ? row.optionsFor(argument.modelData) : []
                        objectName: "macroArgument_" + row.stepIndex + "_" + argument.modelData.id
                        visible: options.length > 0
                        small: true
                        textRole: "text"
                        model: options
                        currentIndex: Math.max(0, options.findIndex(function(o) { return o.id === argument.value }))
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        onActivated: function(i) { row.setArgument(argument.modelData.id, picker.options[i].id) }
                    }
                    Tk.TextField {
                        objectName: "macroText_" + row.stepIndex + "_" + argument.modelData.id
                        visible: !argument.picksName && argument.modelData.kind !== "choice"
                        small: true
                        mono: argument.modelData.kind === "where"
                        text: argument.value
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        onEditingFinished: if (text !== argument.value) row.setArgument(argument.modelData.id, text)
                    }
                }
            }
        }
    }
}
