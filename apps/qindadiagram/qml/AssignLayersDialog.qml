// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Edit ▸ Layers ▸ Assign to Layer (M87, D-095; Visio's Layer dialog): the
// selection goes on exactly the layers ticked — none ticked takes it off
// every layer. A new layer can be made here too. One undo step on OK.
Tk.Dialog {
    id: dialog
    objectName: "diagramAssignLayersDialog"

    required property DiagramService diagram
    property var chosen: []

    title: qsTr("Assign to layer")
    subtitle: qsTr("%n selected item(s)", "", diagram.selection.length)
    primaryText: qsTr("Assign")

    function openFor() {
        chosen = diagram.selectionLayers()
        newName.text = ""
        open()
    }
    function toggle(index, on) {
        const next = chosen.filter(function(i) { return i !== index })
        if (on) next.push(index)
        chosen = next
    }
    onAccepted: diagram.assignSelectionToLayers(chosen)

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Caption {
            visible: dialog.diagram.layers.length === 0
            text: qsTr("This page has no layers yet: name one below.")
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: dialog.diagram.layers
            delegate: Tk.CheckBox {
                required property var modelData
                objectName: "diagramAssignLayer_" + modelData.index
                text: modelData.name
                checked: dialog.chosen.indexOf(modelData.index) >= 0
                tooltip: qsTr("Put the selection on %1").arg(modelData.name)
                onToggled: dialog.toggle(modelData.index, checked)
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.TextField {
                id: newName
                objectName: "diagramAssignNewLayer"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                small: true
                placeholderText: qsTr("New layer")
                tooltip: qsTr("Name of a new layer to put the selection on")
            }
            Tk.Button {
                objectName: "diagramAssignAddLayer"
                small: true
                iconName: "plus"
                text: qsTr("Add")
                enabled: newName.text.trim().length > 0
                onClicked: {
                    const index = dialog.diagram.addLayer(newName.text)
                    if (index >= 0) dialog.toggle(index, true)
                    newName.text = ""
                }
            }
        }
    }
}
