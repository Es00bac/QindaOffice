// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format ▸ Tabs (M70, D-079): Word's Tabs dialog. The selected paragraphs'
// tab stops — position (inches or centimetres; the host speaks twips from
// the text column's edge), alignment (left, center, right, decimal, bar)
// and leader (none, dots, dashes, underline) — set, cleared one by one or
// all at once, and the document's default stop distance. A stop is "set"
// at the position typed; picking one in the list edits it.
Tk.Dialog {
    id: dialog
    signal tabsApplied(var answer)
    property var stops: []
    property int selected: -1
    property real defaultStopPx: 48
    readonly property real twipsPerUnit: units.currentIndex === 0 ? 1440 : 1440 / 2.54
    readonly property string unitSuffix: units.currentIndex === 0 ? qsTr(" in") : qsTr(" cm")
    readonly property var alignmentNames: ["left", "center", "right", "decimal", "bar"]
    readonly property var leaderNames: ["none", "dot", "hyphen", "underscore"]
    objectName: "tabsDialog"
    title: qsTr("Tabs")
    dialogWidth: 460
    primaryText: qsTr("OK")

    function load(info) {
        stops = info.stops.map(function(s) { return { position: s.position, alignment: s.alignment, leader: s.leader } })
        defaultStopPx = info.defaultStop
        defaultStop.value = info.defaultStop * 15 / twipsPerUnit
        select(stops.length > 0 ? 0 : -1)
    }
    function label(stop) {
        return (stop.position / twipsPerUnit).toFixed(2) + unitSuffix
    }
    function select(index) {
        selected = index
        if (index < 0) {
            position.value = 0 // nothing for OK to set again
            return
        }
        const s = stops[index]
        position.value = s.position / twipsPerUnit
        alignmentGroup.pick(Math.max(0, alignmentNames.indexOf(s.alignment)))
        leaderGroup.pick(Math.max(0, leaderNames.indexOf(s.leader)))
    }
    // Word's Set: the stop at the typed position takes the chosen alignment
    // and leader (a new stop when there was none).
    function setStop() {
        const twips = Math.round(position.value * twipsPerUnit)
        if (twips <= 0) return
        const stop = { position: twips, alignment: alignmentNames[alignmentGroup.index],
                       leader: leaderNames[leaderGroup.index] }
        const next = stops.filter(function(s) { return Math.abs(s.position - twips) > 2 })
        next.push(stop)
        next.sort(function(a, b) { return a.position - b.position })
        stops = next
        selected = next.indexOf(stop)
    }
    function clearStop() {
        if (selected < 0) return
        const next = stops.slice()
        next.splice(selected, 1)
        stops = next
        select(Math.min(selected, next.length - 1))
    }
    // OK sets the stop typed in the position field, as Word's does.
    onAccepted: {
        if (position.value > 0) setStop()
        dialog.tabsApplied({ stops: stops, defaultStop: defaultStop.value * twipsPerUnit / 15 })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Units")
            Tk.Segmented {
                id: units
                objectName: "tabsUnits"
                model: [qsTr("Inches"), qsTr("Centimetres")]
                property real previous: 1440
                onCurrentIndexChanged: {
                    const now = currentIndex === 0 ? 1440 : 1440 / 2.54
                    position.value = position.value * previous / now
                    defaultStop.value = defaultStop.value * previous / now
                    previous = now
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Tab stop position")
            Tk.NumberField {
                id: position
                objectName: "tabsPosition"
                from: 0; to: 22; stepSize: 0.25; decimals: 2; suffix: dialog.unitSuffix
                onValueModified: function(v) { value = v }
            }
        }
        Tk.Box {
            objectName: "tabsList"
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Caption { visible: dialog.stops.length === 0; text: qsTr("No tab stops: the default stops apply.") }
                Repeater {
                    model: dialog.stops
                    Tk.ListRow {
                        objectName: "tabStop_" + index
                        text: dialog.label(modelData)
                        secondaryText: qsTr("%1, leader %2").arg(modelData.alignment).arg(modelData.leader)
                        selected: index === dialog.selected
                        onClicked: dialog.select(index)
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Alignment")
            Tk.Flex {
                id: alignmentGroup
                property int index: 0
                function pick(i) { index = i }
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                Repeater {
                    model: [qsTr("Left"), qsTr("Center"), qsTr("Right"), qsTr("Decimal"), qsTr("Bar")]
                    Tk.RadioButton {
                        objectName: "tabsAlign_" + index
                        text: modelData
                        checked: alignmentGroup.index === index
                        onClicked: alignmentGroup.index = index
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Leader")
            Tk.Flex {
                id: leaderGroup
                property int index: 0
                function pick(i) { index = i }
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                Repeater {
                    model: [qsTr("None"), qsTr("Dots ……"), qsTr("Dashes ----"), qsTr("Underline ____")]
                    Tk.RadioButton {
                        objectName: "tabsLeader_" + index
                        text: modelData
                        checked: leaderGroup.index === index
                        onClicked: leaderGroup.index = index
                    }
                }
            }
        }
        Tk.Flex {
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "tabsSet"; text: qsTr("Set"); onClicked: dialog.setStop() }
            Tk.Button { objectName: "tabsClear"; text: qsTr("Clear"); enabled: dialog.selected >= 0; onClicked: dialog.clearStop() }
            Tk.Button { objectName: "tabsClearAll"; text: qsTr("Clear All"); enabled: dialog.stops.length > 0
                        onClicked: { dialog.stops = []; dialog.select(-1) } }
        }
        Tk.PropertyRow {
            label: qsTr("Default tab stops")
            Tk.NumberField {
                id: defaultStop
                objectName: "tabsDefault"
                from: 0.01; to: 22; stepSize: 0.25; decimals: 2; suffix: dialog.unitSuffix
                onValueModified: function(v) { value = v }
            }
        }
    }
}
