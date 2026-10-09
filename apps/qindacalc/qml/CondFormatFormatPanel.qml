// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The format a matching rule gives (M78, D-087; CondFormatRuleDialog.qml):
// fill and font colour, bold, italic, underline, strikethrough — Excel's
// Format Cells for a rule — and Stop If True. The colours are document
// colours (they print), chosen from Excel's highlight palette.
Tk.Flex {
    id: panel

    required property var ruleDialog

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.SectionHeader { title: qsTr("Format") }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        wrap: Tk.Flex.Wrap
        Tk.CheckBox {
            objectName: "condRuleFillOn"
            text: qsTr("Fill")
            checked: (panel.ruleDialog.rule.background || "").length > 0
            onToggled: panel.ruleDialog.set("background", checked ? "#ffc7ce" : "")
        }
        Tk.ColorButton {
            objectName: "condRuleFill"
            small: true
            color: (panel.ruleDialog.rule.background || "").length > 0 ? panel.ruleDialog.rule.background : "#ffc7ce"
            colors: ["#ffc7ce", "#ffeb9c", "#c6efce", "#ddebf7", "#fce4d6", "#ededed", "#ffff00", "#ff0000", "#00b050"]
            onColorSelected: function(c) { panel.ruleDialog.set("background", c.toString()) }
        }
        Tk.CheckBox {
            objectName: "condRuleFontOn"
            text: qsTr("Font color")
            checked: (panel.ruleDialog.rule.foreground || "").length > 0
            onToggled: panel.ruleDialog.set("foreground", checked ? "#9c0006" : "")
        }
        Tk.ColorButton {
            objectName: "condRuleFont"
            small: true
            color: (panel.ruleDialog.rule.foreground || "").length > 0 ? panel.ruleDialog.rule.foreground : "#9c0006"
            colors: ["#9c0006", "#9c5700", "#006100", "#1f4e78", "#c00000", "#000000", "#7030a0", "#ffffff", "#808080"]
            onColorSelected: function(c) { panel.ruleDialog.set("foreground", c.toString()) }
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        wrap: Tk.Flex.Wrap
        Repeater {
            model: [{ key: "bold", text: qsTr("Bold") }, { key: "italic", text: qsTr("Italic") },
                    { key: "underline", text: qsTr("Underline") }, { key: "strikeout", text: qsTr("Strikethrough") }]
            Tk.CheckBox {
                required property var modelData
                objectName: "condRule_" + modelData.key
                text: modelData.text
                checked: panel.ruleDialog.rule[modelData.key] === true
                onToggled: panel.ruleDialog.set(modelData.key, checked)
            }
        }
    }
    Tk.CheckBox {
        objectName: "condRuleStopIfTrue"
        text: qsTr("Stop If True")
        checked: panel.ruleDialog.rule.stopIfTrue === true
        onToggled: panel.ruleDialog.set("stopIfTrue", checked)
    }
}
