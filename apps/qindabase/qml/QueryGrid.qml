// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The design grid: the row labels, one QueryGridColumn per column and Add
// column. Columns keep their width and the grid scrolls sideways in a
// narrow window (Access's grid does the same).
Tk.Scroll {
    id: grid

    required property var view
    readonly property var design: view.design

    implicitHeight: gridRow.implicitHeight + Tk.Theme.size.scrollbar
    overflowX: Tk.Scroll.Auto
    overflowY: Tk.Scroll.Hidden
    accessibleName: qsTr("Design grid")

    Tk.Flex {
        id: gridRow
        objectName: "queryGrid"
        direction: Tk.Flex.Row
        gap: 0
        Tk.Box {
            color: Tk.Theme.color.headerBg
            padding: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: grid.view.rowLabels
                    delegate: Tk.Caption {
                        required property string modelData
                        text: modelData
                        verticalAlignment: Text.AlignVCenter
                        Tk.Flex.basis: grid.view.rowHeight
                        Tk.Flex.shrink: 0
                    }
                }
            }
        }
        Repeater {
            model: grid.design.columns || []
            delegate: QueryGridColumn {
                required property var modelData
                required property int index
                spec: modelData
                columnIndex: index
                fieldChoices: grid.view.fieldChoices
                appendFields: grid.design.kind === "append" && grid.design.appendTable
                              ? grid.view.base.tableFields(grid.design.appendTable).map(function(f) { return f.name }) : []
                totals: grid.design.totals === true
                kind: grid.design.kind
                rowHeight: grid.view.rowHeight
                onEdited: function(patch) { const at = index; grid.view.edit(function(d) { Object.assign(d.columns[at], patch) }) }
                onRemoved: { const at = index; grid.view.edit(function(d) { d.columns.splice(at, 1) }) }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt
            borderLeft: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            padding: Tk.Theme.space.sm
            Tk.Button {
                objectName: "queryAddColumn"
                text: qsTr("Add column")
                iconName: "plus"
                small: true
                onClicked: grid.view.edit(function(d) { d.columns.push(grid.view.blankColumn("", "", " ")) })
            }
        }
    }
}
