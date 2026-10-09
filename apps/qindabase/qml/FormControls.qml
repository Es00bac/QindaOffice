// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// A form's command buttons and subforms (M80, D-089 §4), under the
// record: a button runs its macro (an embedded one or a named one,
// MacroRunner.qml); a subform shows the record's related records from one
// relationship in an editable datasheet (BaseService.subformSheet), new
// ones joining the record. In design each can be removed; Form & Report ▸
// Add Subform / Add Button add them (FormControlDialog.qml).
Tk.Flex {
    id: controls

    required property var view
    readonly property var buttons: view.controls.filter(function(c) { return c.kind === "button" })
    readonly property var subforms: view.controls.filter(function(c) { return c.kind === "subform" })

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.md

    Tk.Flex {
        objectName: "formButtons"
        visible: controls.buttons.length > 0
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.sm
        Repeater {
            model: controls.buttons
            delegate: Tk.Flex {
                id: buttonRow
                required property var modelData
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                align: Tk.Flex.Center
                Tk.Button {
                    objectName: "formButton_" + buttonRow.modelData.id
                    text: buttonRow.modelData.caption
                    iconName: buttonRow.modelData.macro ? "workflow" : "play"
                    tooltip: buttonRow.modelData.macro ? qsTr("Runs the macro %1").arg(buttonRow.modelData.macro)
                                                       : qsTr("Runs %n action(s)", "", (buttonRow.modelData.actions || []).length)
                    enabled: !controls.view.designing
                    onClicked: controls.view.runButton(buttonRow.modelData)
                }
                Tk.IconButton {
                    visible: controls.view.designing
                    iconName: "x"
                    small: true
                    tooltip: qsTr("Remove the button %1").arg(buttonRow.modelData.caption)
                    onClicked: controls.view.removeControl(buttonRow.modelData.id)
                }
            }
        }
    }
    Repeater {
        model: controls.subforms
        delegate: Tk.Card {
            id: card
            required property var modelData
            required property int index
            readonly property DatasheetModel sheet: controls.view.base !== null ? controls.view.base.subformSheet(index) : null
            objectName: "formSubform_" + modelData.id
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Icon { name: "layers"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.accent }
                    Tk.Label { text: card.modelData.caption; font.weight: Font.DemiBold; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.Caption {
                        text: card.sheet !== null ? qsTr("%n record(s)", "", card.sheet.recordCount) : ""
                        Tk.Flex.shrink: 0
                    }
                    Tk.IconButton {
                        visible: controls.view.designing
                        iconName: "x"
                        small: true
                        tooltip: qsTr("Remove the subform %1").arg(card.modelData.caption)
                        onClicked: controls.view.removeControl(card.modelData.id)
                    }
                }
                Item {
                    // AGENT-NOTE: Tk.Flex sizes children by their implicit size only.
                    implicitHeight: Tk.Theme.size.control * 7
                    Tk.Flex.shrink: 0
                    Tk.EditableGrid {
                        objectName: "formSubformGrid_" + card.modelData.id
                        anchors.fill: parent
                        model: card.sheet !== null ? card.sheet.grid : null
                        tooltip: card.modelData.caption
                        alternatingRows: true
                        fillHandleEnabled: false
                        rowHeaderWidth: Tk.Theme.size.control * 1.6
                        columnLabel: function(id, index) { return card.sheet !== null ? card.sheet.columnLabel(id) : id }
                        rowLabel: function(id, index) { return id === "new" ? "✱" : String(index + 1) }
                    }
                }
            }
        }
    }
}
