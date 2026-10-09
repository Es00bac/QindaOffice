// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Conditional Formatting Rules Manager (M78, D-087; the M21 dialog grew
// into Excel's): the sheet's rules in precedence order with what each does
// and where — New Rule and Edit Rule open CondFormatRuleDialog.qml, Delete
// Rule and the two Move buttons change the list at once (one undo step
// each) — and, read-only, the rules QindaCalc keeps for Excel as the file
// wrote them, with why.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var rules: []
    property var kept: []
    property int selected: -1

    signal ruleRequested(int index)

    objectName: "condFormatDialog"
    title: qsTr("Conditional Formatting Rules Manager")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Close")
    secondaryText: ""

    function load() {
        dialog.rules = dialog.host.condFormatRules()
        dialog.kept = dialog.host.keptRuleList()
        if (dialog.selected >= dialog.rules.length) dialog.selected = dialog.rules.length - 1
    }
    // The list back to the host in its new order (indices name the rules).
    function commit(order) {
        dialog.host.setCondFormatRules(order.map(function(i) { return { index: i } }))
        dialog.load()
    }
    function move(delta) {
        const order = dialog.rules.map(function(r) { return r.index })
        const to = dialog.selected + delta
        if (dialog.selected < 0 || to < 0 || to >= order.length) return
        const moved = order.splice(dialog.selected, 1)[0]
        order.splice(to, 0, moved)
        dialog.selected = to
        dialog.commit(order)
    }
    function removeSelected() {
        if (dialog.selected < 0) return
        dialog.commit(dialog.rules.map(function(r) { return r.index }).filter(function(i, at) { return at !== dialog.selected }))
        dialog.selected = Math.min(dialog.selected, dialog.rules.length - 1)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            wrap: Tk.Flex.Wrap
            Tk.Button { objectName: "condFormatNew"; small: true; iconName: "list-plus"; text: qsTr("New Rule…"); onClicked: dialog.ruleRequested(-1) }
            Tk.Button {
                objectName: "condFormatEdit"; small: true; iconName: "pencil"; text: qsTr("Edit Rule…"); available: dialog.selected >= 0
                onClicked: dialog.ruleRequested(dialog.rules[dialog.selected].index)
            }
            Tk.Button {
                objectName: "condFormatRemove"; small: true; iconName: "trash-2"; text: qsTr("Delete Rule"); available: dialog.selected >= 0
                onClicked: dialog.removeSelected()
            }
            Tk.IconButton {
                objectName: "condFormatUp"; iconName: "chevron-up"; tooltip: qsTr("Move Up"); enabled: dialog.selected > 0
                onClicked: dialog.move(-1)
            }
            Tk.IconButton {
                objectName: "condFormatDown"; iconName: "chevron-down"; tooltip: qsTr("Move Down")
                enabled: dialog.selected >= 0 && dialog.selected + 1 < dialog.rules.length
                onClicked: dialog.move(1)
            }
        }
        Tk.SectionHeader { title: qsTr("Rules (applied in the order shown)"); count: String(dialog.rules.length) }
        Tk.Scroll {
            objectName: "condFormatList"
            implicitHeight: Tk.Theme.size.row * 6
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.rules
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "condFormatRule_" + index
                        text: modelData.description
                        secondaryText: qsTr("Applies to %1").arg(modelData.range) + (modelData.stopIfTrue ? qsTr(" · Stop If True") : "")
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                        onDoubleClicked: dialog.ruleRequested(modelData.index)
                    }
                }
                Tk.Caption { visible: dialog.rules.length === 0; text: qsTr("No rules on this sheet."); muted: true }
            }
        }
        Tk.SectionHeader {
            visible: dialog.kept.length > 0
            title: qsTr("Kept as Excel wrote them (not shown here)")
            count: String(dialog.kept.length)
        }
        Repeater {
            model: dialog.kept
            Tk.ListRow {
                required property var modelData
                required property int index
                objectName: "keptRule_" + index
                iconName: "lock"
                enabled: false
                text: modelData.kind + " · " + modelData.range
                secondaryText: modelData.reason
            }
        }
    }
}
