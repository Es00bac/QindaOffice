// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The query's tables (a card each, its fields one click from the grid) and
// how they join (matching rows, or every row of one side).
Tk.Flex {
    id: pane

    required property var view
    readonly property var design: view.design

    function chooseTable() { addMenu.popup(addButton, 0, addButton.height) }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.md

    Tk.Flex {
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.md
        Repeater {
            model: pane.design.tables || []
            delegate: Tk.Card {
                id: tableCard
                required property var modelData
                objectName: "queryTable_" + modelData.name
                width: Tk.Theme.size.fieldWidth
                Tk.Flex.basis: Tk.Theme.size.fieldWidth
                Tk.Flex.shrink: 0
                padding: 0
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Tk.Box {
                        color: Tk.Theme.color.headerBg
                        padding: Tk.Theme.space.xs
                        paddingLeft: Tk.Theme.space.sm
                        Tk.Flex {
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            Tk.Label { text: tableCard.modelData.name; font.weight: Font.DemiBold; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                            Tk.IconButton {
                                iconName: "x"; small: true
                                tooltip: qsTr("Remove %1").arg(tableCard.modelData.name)
                                onClicked: pane.view.removeTable(tableCard.modelData.name)
                            }
                        }
                    }
                    Repeater {
                        model: ["*"].concat(pane.view.base.tableFields(tableCard.modelData.name).map(function(f) { return f.name }))
                        delegate: Tk.ListRow {
                            required property string modelData
                            small: true
                            text: modelData === "*" ? qsTr("All fields") : modelData
                            iconName: modelData === "*" ? "asterisk" : ""
                            tooltip: qsTr("Add %1 to the grid").arg(modelData)
                            onClicked: pane.view.addColumn(tableCard.modelData.name, modelData)
                        }
                    }
                }
            }
        }
        Tk.Button {
            id: addButton
            objectName: "queryAddTable"
            text: qsTr("Add table")
            iconName: "plus"
            onClicked: pane.chooseTable()
        }
    }
    Tk.Flex {
        visible: (pane.design.joins || []).length > 0
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        Tk.Overline { title: qsTr("JOINS") }
        Repeater {
            model: pane.design.joins || []
            delegate: Tk.Box {
                id: joinBox
                required property var modelData
                required property int index
                objectName: "queryJoin_" + index
                color: Tk.Theme.color.chipBg
                radius: Tk.Theme.radius.md
                padding: Tk.Theme.space.xs
                paddingLeft: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Icon { name: "link"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.accent }
                    Tk.Label {
                        mono: true
                        text: joinBox.modelData.leftTable + "." + joinBox.modelData.leftField + " = "
                              + joinBox.modelData.rightTable + "." + joinBox.modelData.rightField
                        Tk.Flex.shrink: 0
                    }
                    Tk.ComboBox {
                        small: true
                        model: [qsTr("matching rows"), qsTr("all %1").arg(joinBox.modelData.leftTable),
                                qsTr("all %1").arg(joinBox.modelData.rightTable)]
                        currentIndex: joinBox.modelData.kind === "left" ? 1 : joinBox.modelData.kind === "right" ? 2 : 0
                        tooltip: qsTr("Which rows the join keeps")
                        onActivated: function(i) {
                            const at = joinBox.index
                            pane.view.edit(function(d) { d.joins[at].kind = ["inner", "left", "right"][i] })
                        }
                    }
                    Tk.IconButton {
                        iconName: "x"; small: true; tooltip: qsTr("Remove join")
                        onClicked: { const at = joinBox.index; pane.view.edit(function(d) { d.joins.splice(at, 1) }) }
                    }
                }
            }
        }
    }

    Tk.Menu {
        id: addMenu
        Repeater {
            model: pane.view.base !== null ? pane.view.base.tableNames : []
            delegate: Tk.MenuItem {
                required property string modelData
                text: modelData
                iconName: "table"
                onTriggered: pane.view.addTable(modelData)
            }
        }
    }
}
