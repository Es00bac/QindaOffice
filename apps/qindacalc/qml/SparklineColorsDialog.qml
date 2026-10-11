// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// The Sparkline tab's Sparkline Color and Marker Color (M90, D-098): per
// part — the line or columns, negative points, markers, high, low, first
// and last points, the axis — a theme colour (Accent 1–6, Text and
// Background 1–2) with Excel's shades, which follows the workbook's theme
// as Excel's do, or a colour of one's own. One undo step for the groups.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var state: ({})
    property var picked: ({})

    objectName: "sparklineColorsDialog"
    title: qsTr("Sparkline Colors")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("OK")

    readonly property var parts: [
        { key: "series", label: qsTr("Sparkline") }, { key: "negative", label: qsTr("Negative Points") },
        { key: "markers", label: qsTr("Markers") }, { key: "high", label: qsTr("High Point") },
        { key: "low", label: qsTr("Low Point") }, { key: "first", label: qsTr("First Point") },
        { key: "last", label: qsTr("Last Point") }, { key: "axis", label: qsTr("Axis") }]
    readonly property var roles: dialog.host.sparklines.colorRoles()

    function load() {
        dialog.state = dialog.host.sparklines.colorsState()
        dialog.picked = JSON.parse(JSON.stringify(dialog.state))
    }
    function set(key, field, value) {
        const next = Object.assign({}, dialog.picked)
        next[key] = Object.assign({}, next[key] || {})
        next[key][field] = value
        dialog.picked = next
    }
    onAccepted: dialog.host.sparklines.applyColors(dialog.picked)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater {
            model: dialog.parts
            Tk.PropertyRow {
                required property var modelData
                label: modelData.label
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    wrap: Tk.Flex.Wrap
                    Tk.ComboBox {
                        objectName: "sparklineRole_" + modelData.key
                        small: true
                        model: dialog.roles
                        currentIndex: dialog.picked[modelData.key] ? dialog.picked[modelData.key].role : 0
                        onActivated: function(index) { dialog.set(modelData.key, "role", index) }
                    }
                    Tk.ComboBox {
                        objectName: "sparklineShade_" + modelData.key
                        small: true
                        model: dialog.host.sparklines.shades()
                        enabled: dialog.picked[modelData.key] !== undefined && dialog.picked[modelData.key].role < dialog.roles.length - 1
                        currentIndex: dialog.picked[modelData.key] ? dialog.picked[modelData.key].shade : 0
                        onActivated: function(index) { dialog.set(modelData.key, "shade", index) }
                    }
                    Tk.ColorButton {
                        objectName: "sparklineCustom_" + modelData.key
                        small: true
                        tooltip: qsTr("A colour of your own")
                        color: dialog.picked[modelData.key] ? dialog.picked[modelData.key].color : "black"
                        colors: ["#000000", "#7f7f7f", "#376092", "#c0504d", "#9bbb59", "#f79646", "#8064a2", "#4bacc6", "#ffffff"]
                        onColorSelected: function(c) {
                            dialog.set(modelData.key, "color", c.toString())
                            dialog.set(modelData.key, "role", dialog.roles.length - 1)
                        }
                    }
                }
            }
        }
    }
}
