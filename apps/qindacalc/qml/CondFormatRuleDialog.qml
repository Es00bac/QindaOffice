// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// New Formatting Rule / Edit Formatting Rule (M78, D-087): Excel's six rule
// types — every cell by its value (colour scales, data bars, icon sets:
// CondFormatScalePanel.qml), only cells that contain a value, text, a date
// or blanks/errors, top or bottom ranked values, values above or below
// the average, unique or duplicate values, and a formula — with the
// format (fill, font colour, bold, italic, underline, strikethrough) and
// Stop If True. Edits one rule map (calc_condformat_maps.h); OK saves it
// as one undo step (a new rule goes first, Excel's precedence).
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int index: -1
    property var rule: ({})
    property string error: ""
    // CondFormatRule::Op values (sheet_rules.h).
    readonly property var op: ({ gt: 0, lt: 1, between: 2, eq: 3, ne: 4, contains: 5, blanks: 6, noBlanks: 7, ge: 8, le: 9,
                                 expression: 10, dataBar: 11, notBetween: 12, notContains: 13, beginsWith: 14, endsWith: 15,
                                 errors: 16, noErrors: 17, top10: 18, average: 19, duplicate: 20, unique: 21, period: 22,
                                 colorScale: 23, iconSet: 24 })
    readonly property var cellOps: [2, 12, 3, 4, 0, 1, 8, 9]
    readonly property var textOps: [5, 13, 14, 15]
    readonly property int kind: {
        const o = dialog.rule.op
        if (o === 11 || o === 23 || o === 24) return 0
        if (o === 18) return 2
        if (o === 19) return 3
        if (o === 20 || o === 21) return 4
        if (o === 10) return 5
        return 1
    }
    readonly property bool formats: dialog.kind !== 0

    signal saved()

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "condFormatRuleDialog"
    title: dialog.index < 0 ? qsTr("New Formatting Rule") : qsTr("Edit Formatting Rule")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("OK")

    function load(index) {
        dialog.rule = dialog.host.condFormatRuleState(index)
        dialog.index = dialog.rule.index !== undefined ? dialog.rule.index : -1
        dialog.error = ""
    }
    function set(key, value) {
        const next = Object.assign({}, dialog.rule)
        next[key] = value
        dialog.rule = next
    }
    function setKind(kind) {
        if (kind === dialog.kind) return
        const next = Object.assign({}, dialog.rule)
        next.op = [dialog.op.colorScale, dialog.op.gt, dialog.op.top10, dialog.op.average, dialog.op.duplicate,
                   dialog.op.expression][kind]
        // "Based on their values" starts as Excel's does: a 2-Color Scale
        // from the lowest value to the highest (CondFormatScalePanel.qml).
        const points = next.thresholds || []
        if (kind === 0 && (points.length < 2 || (next.colors || []).length !== points.length)) {
            next.thresholds = [{ type: 0, value: "", gte: true }, { type: 1, value: "", gte: true }]
            next.colors = ["#f8696b", "#63be7b"]
        }
        dialog.rule = next
    }
    onAccepted: {
        const problem = dialog.host.saveCondFormatRule(dialog.index, dialog.rule)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
            return
        }
        dialog.saved()
    }

    Loader {
        active: dialog.visible
        sourceComponent: Component {
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Notice { objectName: "condRuleError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
                Tk.PropertyRow {
                    label: qsTr("Applies to")
                    Tk.TextField {
                        objectName: "condRuleRange"; mono: true; text: dialog.rule.range || ""
                        onTextEdited: dialog.set("range", text)
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Rule type")
                    Tk.ComboBox {
                        objectName: "condRuleKind"
                        width: Tk.Theme.size.fieldWidth * 3
                        model: [qsTr("Format all cells based on their values"), qsTr("Format only cells that contain"),
                                qsTr("Format only top or bottom ranked values"),
                                qsTr("Format only values that are above or below average"),
                                qsTr("Format only unique or duplicate values"), qsTr("Use a formula to determine which cells to format")]
                        currentIndex: dialog.kind
                        onActivated: function(i) { dialog.setKind(i) }
                    }
                }
                // AGENT-GUARD: not `dialog: dialog` — inside the Loader's component a
                // panel's own property of that name shadows this file's id.
                CondFormatScalePanel { visible: dialog.kind === 0; ruleDialog: dialog }
                // Format only cells that contain …
                Tk.Flex {
                    visible: dialog.kind === 1
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    wrap: Tk.Flex.Wrap
                    Tk.ComboBox {
                        objectName: "condRuleContains"
                        model: [qsTr("Cell Value"), qsTr("Specific Text"), qsTr("Dates Occurring"), qsTr("Blanks"), qsTr("No Blanks"),
                                qsTr("Errors"), qsTr("No Errors")]
                        readonly property int which: dialog.cellOps.indexOf(dialog.rule.op) >= 0 ? 0
                            : dialog.textOps.indexOf(dialog.rule.op) >= 0 ? 1 : dialog.rule.op === 22 ? 2 : dialog.rule.op === 6 ? 3
                            : dialog.rule.op === 7 ? 4 : dialog.rule.op === 16 ? 5 : 6
                        currentIndex: which
                        onActivated: function(i) { dialog.set("op", [0, 5, 22, 6, 7, 16, 17][i]) }
                    }
                    Tk.ComboBox {
                        objectName: "condRuleOperator"
                        visible: dialog.cellOps.indexOf(dialog.rule.op) >= 0
                        model: [qsTr("between"), qsTr("not between"), qsTr("equal to"), qsTr("not equal to"), qsTr("greater than"),
                                qsTr("less than"), qsTr("greater than or equal to"), qsTr("less than or equal to")]
                        currentIndex: Math.max(0, dialog.cellOps.indexOf(dialog.rule.op))
                        onActivated: function(i) { dialog.set("op", dialog.cellOps[i]) }
                    }
                    Tk.TextField {
                        objectName: "condRuleValue1"
                        visible: dialog.cellOps.indexOf(dialog.rule.op) >= 0
                        mono: true
                        text: dialog.rule.formula ? "=" + dialog.rule.formula : String(dialog.rule.first)
                        onTextEdited: dialog.setOperand(1, text)
                    }
                    Tk.Label { visible: dialog.rule.op === 2 || dialog.rule.op === 12; text: qsTr("and") }
                    Tk.TextField {
                        objectName: "condRuleValue2"
                        visible: dialog.rule.op === 2 || dialog.rule.op === 12
                        mono: true
                        text: dialog.rule.formula2 ? "=" + dialog.rule.formula2 : String(dialog.rule.second)
                        onTextEdited: dialog.setOperand(2, text)
                    }
                    Tk.ComboBox {
                        objectName: "condRuleTextOperator"
                        visible: dialog.textOps.indexOf(dialog.rule.op) >= 0
                        model: [qsTr("containing"), qsTr("not containing"), qsTr("beginning with"), qsTr("ending with")]
                        currentIndex: Math.max(0, dialog.textOps.indexOf(dialog.rule.op))
                        onActivated: function(i) { dialog.set("op", dialog.textOps[i]) }
                    }
                    Tk.TextField {
                        objectName: "condRuleText"
                        visible: dialog.textOps.indexOf(dialog.rule.op) >= 0
                        text: dialog.rule.text || ""
                        onTextEdited: dialog.set("text", text)
                    }
                    Tk.ComboBox {
                        objectName: "condRulePeriod"
                        visible: dialog.rule.op === 22
                        model: [qsTr("Today"), qsTr("Yesterday"), qsTr("Tomorrow"), qsTr("In the last 7 days"), qsTr("This week"),
                                qsTr("Last week"), qsTr("Next week"), qsTr("This month"), qsTr("Last month"), qsTr("Next month")]
                        currentIndex: dialog.rule.period || 0
                        onActivated: function(i) { dialog.set("period", i) }
                    }
                }
                // Top or bottom ranked values
                Tk.Flex {
                    visible: dialog.kind === 2
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    align: Tk.Flex.Center
                    Tk.ComboBox {
                        objectName: "condRuleTopBottom"; model: [qsTr("Top"), qsTr("Bottom")]
                        currentIndex: dialog.rule.bottom === true ? 1 : 0
                        onActivated: function(i) { dialog.set("bottom", i === 1) }
                    }
                    Tk.NumberField {
                        objectName: "condRuleRank"; from: 1; to: 1000; value: dialog.rule.rank || 10
                        onValueModified: function(v) { dialog.set("rank", Math.round(v)) }
                    }
                    Tk.CheckBox {
                        objectName: "condRulePercent"; text: qsTr("% of the selected range"); checked: dialog.rule.percent === true
                        onToggled: dialog.set("percent", checked)
                    }
                }
                // Above or below average
                Tk.ComboBox {
                    objectName: "condRuleAverage"
                    visible: dialog.kind === 3
                    model: [qsTr("above"), qsTr("below"), qsTr("equal or above"), qsTr("equal or below"), qsTr("1 std dev above"),
                            qsTr("2 std dev above"), qsTr("3 std dev above"), qsTr("1 std dev below"), qsTr("2 std dev below"),
                            qsTr("3 std dev below")]
                    readonly property int which: dialog.rule.stdDev > 0 ? (dialog.rule.aboveAverage ? 3 : 6) + dialog.rule.stdDev
                                                                        : (dialog.rule.aboveAverage ? 0 : 1) + (dialog.rule.equalAverage ? 2 : 0)
                    currentIndex: which
                    onActivated: function(i) {
                        const next = Object.assign({}, dialog.rule)
                        next.aboveAverage = i === 0 || i === 2 || (i >= 4 && i <= 6)
                        next.equalAverage = i === 2 || i === 3
                        next.stdDev = i >= 7 ? i - 6 : (i >= 4 ? i - 3 : 0)
                        dialog.rule = next
                    }
                }
                Tk.Segmented {
                    objectName: "condRuleDuplicate"
                    visible: dialog.kind === 4
                    model: [qsTr("duplicate"), qsTr("unique")]
                    currentIndex: dialog.rule.op === 21 ? 1 : 0
                    onActivated: function(i) { dialog.set("op", i === 1 ? 21 : 20) }
                }
                Tk.PropertyRow {
                    visible: dialog.kind === 5
                    label: qsTr("Format values where this formula is true")
                    Tk.TextField {
                        objectName: "condRuleFormula"; mono: true; text: "=" + (dialog.rule.formula || "")
                        onTextEdited: dialog.set("formula", text.replace(/^=/, ""))
                    }
                }
                CondFormatFormatPanel { visible: dialog.formats; ruleDialog: dialog }
            }
        }
    }

    // A cellIs operand: a number, or "=…" (a cell, a name, a formula).
    function setOperand(which, text) {
        const t = text.trim()
        const next = Object.assign({}, dialog.rule)
        const number = Number(t)
        if (t.length > 0 && !t.startsWith("=") && isFinite(number)) {
            next[which === 1 ? "first" : "second"] = number
            next[which === 1 ? "formula" : "formula2"] = ""
        } else {
            next[which === 1 ? "formula" : "formula2"] = t.replace(/^=/, "")
        }
        dialog.rule = next
    }
}
