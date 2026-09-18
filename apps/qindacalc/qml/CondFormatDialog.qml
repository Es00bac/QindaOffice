// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Conditional formatting for the sheet: the existing rules (remove one),
// and a new rule over the selection. The whole list goes back to the host
// on Apply, so one visit is ONE undo step.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var rules: []
    property string range: ""
    property int selected: -1

    objectName: "condFormatDialog"
    title: qsTr("Conditional formatting")
    dialogWidth: 520
    primaryText: qsTr("Apply")

    readonly property var ops: [qsTr("greater than"), qsTr("less than"), qsTr("between"), qsTr("equal to"),
                                qsTr("not equal to"), qsTr("contains text"), qsTr("is empty"), qsTr("is not empty")]
    readonly property int op: opBox.currentIndex
    readonly property bool numeric: op <= 4
    readonly property bool textual: op === 5 || op === 3 || op === 4

    function load() {
        dialog.rules = dialog.host.condFormatRules()
        dialog.range = dialog.host.selectionRangeText()
        dialog.selected = -1
    }
    function describe(rule) {
        let condition = dialog.ops[rule.op]
        if (rule.op <= 1 || rule.op === 3 || rule.op === 4) condition += " " + rule.first
        if (rule.op === 2) condition += " " + rule.first + " – " + rule.second
        if (rule.op === 5) condition += " “" + rule.text + "”"
        return rule.range + ": " + condition
    }
    function addRule() {
        const list = dialog.rules.slice()
        list.push({ range: dialog.range, op: dialog.op, first: first.value, second: second.value, text: text.text,
                    background: bgOn.checked ? background.color.toString() : "",
                    foreground: fgOn.checked ? foreground.color.toString() : "",
                    bold: bold.checked, italic: italic.checked })
        dialog.rules = list
    }
    function removeSelected() {
        if (dialog.selected < 0) return
        const list = dialog.rules.slice()
        list.splice(dialog.selected, 1)
        dialog.rules = list
        dialog.selected = -1
    }
    onAccepted: dialog.host.setCondFormatRules(dialog.rules)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Rules on this sheet") }
        Tk.Scroll {
            objectName: "condFormatList"
            implicitHeight: 110
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.rules
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "condFormatRule_" + index
                        text: dialog.describe(modelData)
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                    }
                }
                Tk.Caption { visible: dialog.rules.length === 0; text: qsTr("No rules yet."); muted: true }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Spacer { }
            Tk.Button { objectName: "condFormatRemove"; text: qsTr("Remove rule"); small: true; available: dialog.selected >= 0; onClicked: dialog.removeSelected() }
        }
        Tk.Divider { }
        Tk.Overline { text: qsTr("New rule for %1").arg(dialog.range) }
        Tk.PropertyRow { label: qsTr("Condition"); Tk.ComboBox { id: opBox; objectName: "condFormatOp"; model: dialog.ops } }
        Tk.PropertyRow { label: qsTr("Value"); Tk.NumberField { id: first; objectName: "condFormatFirst"; enabled: dialog.numeric; from: -1e12; to: 1e12; decimals: 4 } }
        Tk.PropertyRow { label: qsTr("and"); Tk.NumberField { id: second; objectName: "condFormatSecond"; enabled: dialog.op === 2; from: -1e12; to: 1e12; decimals: 4 } }
        Tk.PropertyRow { label: qsTr("Text"); Tk.TextField { id: text; objectName: "condFormatText"; enabled: dialog.textual } }
        Tk.PropertyRow {
            label: qsTr("Format")
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                Tk.CheckBox { id: bgOn; text: qsTr("Fill") }
                Tk.ColorButton { id: background; objectName: "condFormatBackground"; small: true; color: "#fff3b0"; enabled: bgOn.checked; onColorSelected: bgOn.checked = true }
                Tk.CheckBox { id: fgOn; text: qsTr("Text") }
                Tk.ColorButton { id: foreground; objectName: "condFormatForeground"; small: true; color: "#d21c1c"; enabled: fgOn.checked; onColorSelected: fgOn.checked = true }
                Tk.CheckBox { id: bold; objectName: "condFormatBold"; text: qsTr("Bold") }
                Tk.CheckBox { id: italic; objectName: "condFormatItalic"; text: qsTr("Italic") }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Spacer { }
            Tk.Button { objectName: "condFormatAdd"; text: qsTr("Add rule"); small: true; onClicked: dialog.addRule() }
        }
    }
}
