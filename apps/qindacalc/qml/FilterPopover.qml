// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The autofilter dropdown: a tri-state "Select all" over a checkable list
// of the column's values and OK / Cancel. AGENT-NOTE: one popover with its
// own OK — a menu of checkable entries would close on the first click, so
// picking three values would take three trips.
Tk.Popover {
    id: popover

    property int column: -1
    property var values: []
    property var checked: ({})
    signal filterApplied(int column, var kept)

    objectName: "calcFilterMenu"
    padding: Tk.Theme.space.sm

    function show(column, values, allowed, point) {
        popover.column = column
        popover.values = values
        const map = {}
        for (let i = 0; i < values.length; ++i) map[values[i]] = allowed.indexOf(values[i]) >= 0
        popover.checked = map
        popover.x = point.x
        popover.y = point.y
        popover.open()
    }
    function keptValues() {
        const kept = []
        for (let i = 0; i < popover.values.length; ++i) if (popover.checked[popover.values[i]]) kept.push(popover.values[i])
        return kept
    }
    readonly property int checkedCount: {
        let n = 0
        for (let i = 0; i < popover.values.length; ++i) if (popover.checked[popover.values[i]]) ++n
        return n
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.CheckBox {
            objectName: "calcFilterSelectAll"
            text: qsTr("Select all")
            tristate: true
            checkState: popover.checkedCount === 0 ? Qt.Unchecked
                      : popover.checkedCount === popover.values.length ? Qt.Checked : Qt.PartiallyChecked
            onClicked: {
                const on = popover.checkedCount < popover.values.length
                const map = {}
                for (let i = 0; i < popover.values.length; ++i) map[popover.values[i]] = on
                popover.checked = map
            }
        }
        Tk.Divider { }
        Tk.Scroll {
            objectName: "calcFilterList"
            implicitWidth: 200
            implicitHeight: Math.min(240, popover.values.length * 22 + 4)
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: popover.values
                    Tk.CheckBox {
                        required property string modelData
                        objectName: "calcFilterValue_" + modelData
                        text: modelData.length > 0 ? modelData : qsTr("(empty)")
                        checked: popover.checked[modelData] === true
                        onToggled: { const map = Object.assign({}, popover.checked); map[modelData] = checked; popover.checked = map }
                    }
                }
            }
        }
        Tk.Flex {
            objectName: "calcFilterButtons"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Spacer { }
            Tk.Button { text: qsTr("Cancel"); small: true; onClicked: popover.close() }
            Tk.Button { objectName: "calcFilterOk"; text: qsTr("OK"); small: true; variant: "accent"; onClicked: { popover.filterApplied(popover.column, popover.keptValues()); popover.close() } }
        }
    }
}
