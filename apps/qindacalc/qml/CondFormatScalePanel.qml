// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format all cells based on their values (M78, D-087;
// CondFormatRuleDialog.qml): 2-Color Scale, 3-Color Scale, Data Bar and
// Icon Sets with their thresholds (Lowest / Number / Percent / Formula /
// Percentile / Highest / Automatic — Excel's cfvo), colours, the bar's
// fill, border, negative colour, axis and direction, the icon style,
// Reverse Icon Order and Show only the bar / icon.
Tk.Flex {
    id: panel

    required property var ruleDialog
    readonly property var rule: panel.ruleDialog.rule
    readonly property int style: panel.rule.op === 11 ? 2 : panel.rule.op === 24 ? 3
                                                                        : ((panel.rule.thresholds || []).length >= 3 ? 1 : 0)
    // CfThreshold::Type names, Excel's words.
    readonly property var typeNames: [qsTr("Lowest Value"), qsTr("Highest Value"), qsTr("Number"), qsTr("Percent"),
                                      qsTr("Percentile"), qsTr("Formula"), qsTr("Automatic"), qsTr("Automatic")]
    readonly property var iconSets: ["3Arrows", "3ArrowsGray", "3Flags", "3TrafficLights1", "3TrafficLights2", "3Signs",
                                     "3Symbols", "3Symbols2", "3Stars", "3Triangles", "4Arrows", "4ArrowsGray", "4RedToBlack",
                                     "4Rating", "4TrafficLights", "5Arrows", "5ArrowsGray", "5Rating", "5Quarters", "5Boxes"]

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function setStyle(style) {
        const next = Object.assign({}, panel.rule)
        if (style === 0) {
            next.op = 23; next.thresholds = [{ type: 0, value: "", gte: true }, { type: 1, value: "", gte: true }]
            next.colors = ["#f8696b", "#63be7b"]
        } else if (style === 1) {
            next.op = 23
            next.thresholds = [{ type: 0, value: "", gte: true }, { type: 4, value: "50", gte: true }, { type: 1, value: "", gte: true }]
            next.colors = ["#f8696b", "#ffeb84", "#63be7b"]
        } else if (style === 2) {
            next.op = 11; next.thresholds = [{ type: 6, value: "", gte: true }, { type: 7, value: "", gte: true }]
            next.barColor = next.barColor || "#638ec6"
        } else {
            panel.setIcons(next, "3TrafficLights1")
            return
        }
        panel.ruleDialog.rule = next
    }
    function setIcons(next, set) {
        const count = Number(set.charAt(0))
        next.op = 24
        next.iconSet = set
        next.thresholds = []
        for (let i = 0; i < count; ++i)
            next.thresholds.push({ type: 3, value: String(Math.round(i * 100 / count)), gte: true })
        panel.ruleDialog.rule = next
    }
    function setThreshold(i, key, value) {
        const next = Object.assign({}, panel.rule)
        next.thresholds = next.thresholds.slice()
        next.thresholds[i] = Object.assign({}, next.thresholds[i])
        next.thresholds[i][key] = value
        panel.ruleDialog.rule = next
    }
    function setColor(i, color) {
        const next = Object.assign({}, panel.rule)
        next.colors = next.colors.slice()
        next.colors[i] = color
        panel.ruleDialog.rule = next
    }

    Tk.PropertyRow {
        label: qsTr("Format style")
        Tk.ComboBox {
            objectName: "condRuleStyle"
            model: [qsTr("2-Color Scale"), qsTr("3-Color Scale"), qsTr("Data Bar"), qsTr("Icon Sets")]
            currentIndex: panel.style
            onActivated: function(i) { panel.setStyle(i) }
        }
    }
    // The thresholds: Minimum (Midpoint) Maximum, or one per icon.
    Repeater {
        model: (panel.rule.thresholds || []).length
        Tk.Flex {
            id: point
            required property int index
            readonly property var threshold: panel.rule.thresholds[point.index] || ({})
            visible: !(panel.style === 3 && point.index === 0)
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Caption {
                text: panel.style === 3 ? qsTr("Icon %1 when value is").arg(point.index + 1)
                    : point.index === 0 ? qsTr("Minimum") : (point.index === panel.rule.thresholds.length - 1 ? qsTr("Maximum") : qsTr("Midpoint"))
            }
            Tk.ComboBox {
                objectName: "condRuleGte_" + point.index
                visible: panel.style === 3
                small: true
                model: [">=", ">"]
                currentIndex: point.threshold.gte === false ? 1 : 0
                onActivated: function(i) { panel.setThreshold(point.index, "gte", i === 0) }
            }
            Tk.ComboBox {
                objectName: "condRuleType_" + point.index
                small: true
                model: panel.typeNames
                currentIndex: point.threshold.type || 0
                onActivated: function(i) { panel.setThreshold(point.index, "type", i) }
            }
            Tk.TextField {
                objectName: "condRuleThreshold_" + point.index
                small: true
                mono: true
                enabled: [2, 3, 4, 5].indexOf(point.threshold.type) >= 0
                text: point.threshold.value || ""
                onTextEdited: panel.setThreshold(point.index, "value", text)
            }
            Tk.ColorButton {
                objectName: "condRuleScaleColor_" + point.index
                visible: panel.style <= 1
                small: true
                color: (panel.rule.colors || [])[point.index] || "#ffffff"
                colors: ["#f8696b", "#ffeb84", "#63be7b", "#5a8ac6", "#fcfcff", "#ffef9c", "#ff7128", "#000000", "#ffffff"]
                onColorSelected: function(c) { panel.setColor(point.index, c.toString()) }
            }
        }
    }
    // Data Bar: Excel 2010's bar appearance.
    Tk.Flex {
        visible: panel.style === 2
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        wrap: Tk.Flex.Wrap
        Tk.Caption { text: qsTr("Bar") }
        Tk.ColorButton {
            objectName: "condRuleBarColor"; small: true; color: panel.rule.barColor || "#638ec6"
            colors: ["#638ec6", "#63c384", "#ff555a", "#ffb628", "#008aef", "#d6007b"]
            onColorSelected: function(c) { panel.ruleDialog.set("barColor", c.toString()) }
        }
        Tk.ComboBox {
            objectName: "condRuleBarFill"; small: true; model: [qsTr("Gradient Fill"), qsTr("Solid Fill")]
            currentIndex: panel.rule.barGradient === false ? 1 : 0
            onActivated: function(i) { panel.ruleDialog.set("barGradient", i === 0) }
        }
        Tk.CheckBox {
            objectName: "condRuleBarBorder"; text: qsTr("Border"); checked: (panel.rule.barBorder || "").length > 0
            onToggled: panel.ruleDialog.set("barBorder", checked ? (panel.rule.barColor || "#638ec6") : "")
        }
        Tk.Caption { text: qsTr("Negative") }
        Tk.ColorButton {
            objectName: "condRuleBarNegative"; small: true; color: panel.rule.barNegative || "#ff0000"
            colors: ["#ff0000", "#c00000", "#ff7128", "#808080"]
            onColorSelected: function(c) { panel.ruleDialog.set("barNegative", c.toString()) }
        }
        Tk.ComboBox {
            objectName: "condRuleBarAxis"; small: true
            model: [qsTr("Automatic axis"), qsTr("Cell midpoint"), qsTr("No axis")]
            currentIndex: panel.rule.barAxis || 0
            onActivated: function(i) { panel.ruleDialog.set("barAxis", i) }
        }
        Tk.ComboBox {
            objectName: "condRuleBarDirection"; small: true
            model: [qsTr("Context"), qsTr("Left-to-Right"), qsTr("Right-to-Left")]
            currentIndex: panel.rule.barDirection || 0
            onActivated: function(i) { panel.ruleDialog.set("barDirection", i) }
        }
    }
    // Icon Sets: the style and order.
    Tk.Flex {
        visible: panel.style === 3
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        Tk.ComboBox {
            objectName: "condRuleIconSet"
            model: panel.iconSets
            currentIndex: Math.max(0, panel.iconSets.indexOf(panel.rule.iconSet))
            onActivated: function(i) { panel.setIcons(Object.assign({}, panel.rule), panel.iconSets[i]) }
        }
        Tk.CheckBox {
            objectName: "condRuleReverse"; text: qsTr("Reverse Icon Order"); checked: panel.rule.reverse === true
            onToggled: panel.ruleDialog.set("reverse", checked)
        }
    }
    Tk.CheckBox {
        objectName: "condRuleShowValueOff"
        visible: panel.style >= 2
        text: panel.style === 2 ? qsTr("Show Bar Only") : qsTr("Show Icon Only")
        checked: panel.rule.showValue === false
        onToggled: panel.ruleDialog.set("showValue", !checked)
    }
}
