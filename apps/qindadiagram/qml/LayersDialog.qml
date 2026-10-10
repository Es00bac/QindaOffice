// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Edit ▸ Layers ▸ Layer Properties (M87, D-095; Visio's dialog of the same
// name): the current page's layers, one row each — name, Visible, Print,
// Lock, Active (new shapes go on it), Glue (connectors may glue to its
// shapes), a colour that replaces its shapes' line and text colours, and
// how many items are on it. Every change is one undo step at once; New and
// Remove add and drop layers (items keep their other layers).
Tk.Dialog {
    id: dialog
    objectName: "diagramLayersDialog"

    required property DiagramService diagram
    readonly property var layers: diagram.layers
    // Layer colours are document content (the drawing's own colours, as
    // ColorDialog's swatches are), not chrome.
    readonly property var colors: ["#c00000", "#e36c09", "#2f855a", "#2f6fb2", "#6b46c1", "#64748b"]

    title: qsTr("Layer properties")
    subtitle: qsTr("Layers of page %1").arg(diagram.page.name || "")
    primaryText: qsTr("Done")
    secondaryText: ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.35

    function openFor() { newName.text = ""; open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Caption {
            visible: dialog.layers.length === 0
            text: qsTr("This page has no layers yet. Layers group shapes you show, hide, lock or leave off paper together.")
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: dialog.layers
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "diagramLayerRow_" + modelData.index
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.TextField {
                    objectName: "diagramLayerName"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    small: true
                    text: row.modelData.name
                    tooltip: qsTr("Name of the layer")
                    onEditingFinished: if (text.trim() !== row.modelData.name)
                        dialog.diagram.setLayerProperty(row.modelData.index, "name", text.trim())
                }
                Repeater {
                    model: [{ key: "visible", label: qsTr("Visible") }, { key: "print", label: qsTr("Print") },
                            { key: "locked", label: qsTr("Lock") }, { key: "active", label: qsTr("Active") },
                            { key: "glue", label: qsTr("Glue") }]
                    delegate: Tk.CheckBox {
                        required property var modelData
                        objectName: "diagramLayer_" + modelData.key
                        small: true
                        text: modelData.label
                        checked: !!row.modelData[modelData.key]
                        tooltip: modelData.label
                        onToggled: dialog.diagram.setLayerProperty(row.modelData.index, modelData.key, checked)
                    }
                }
                Tk.ColorButton {
                    objectName: "diagramLayerColor"
                    small: true
                    color: row.modelData.color || Tk.Theme.color.border
                    colors: dialog.colors
                    tooltip: row.modelData.color ? qsTr("Layer colour") : qsTr("No layer colour: shapes keep their own")
                    onColorSelected: function(color) { dialog.diagram.setLayerProperty(row.modelData.index, "color", String(color)) }
                }
                Tk.IconButton {
                    objectName: "diagramLayerClearColor"
                    small: true
                    iconName: "x"
                    visible: !!row.modelData.color
                    tooltip: qsTr("Remove the layer colour")
                    onClicked: dialog.diagram.setLayerProperty(row.modelData.index, "color", "")
                }
                Tk.Caption {
                    objectName: "diagramLayerCount"
                    text: qsTr("%n item(s)", "", row.modelData.count)
                }
                Tk.IconButton {
                    objectName: "diagramLayerRemove"
                    small: true
                    iconName: "trash-2"
                    tooltip: qsTr("Remove the layer (its shapes stay)")
                    onClicked: dialog.diagram.removeLayer(row.modelData.index)
                }
            }
        }
        Tk.Divider {}
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.TextField {
                id: newName
                objectName: "diagramNewLayerName"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                small: true
                placeholderText: qsTr("New layer name")
                tooltip: qsTr("Name of a new layer")
                onAccepted: addLayer.clicked()
            }
            Tk.Button {
                id: addLayer
                objectName: "diagramAddLayer"
                small: true
                iconName: "plus"
                text: qsTr("New layer")
                onClicked: {
                    dialog.diagram.addLayer(newName.text)
                    newName.text = ""
                }
            }
        }
    }
}
