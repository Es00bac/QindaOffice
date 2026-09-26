// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Shape data (D-050, Visio's "Shape Data"): the selected item's key/value
// fields — an org chart card's e-mail and phone, a server's IP address, a
// process step's owner. Each value stores on Enter or when the field loses
// focus; a new field is a name and a value; every change is one undo step.
Tk.Flex {
    id: section
    objectName: "diagramInspectorData"

    required property DiagramService diagram
    readonly property var item: diagram.selected
    readonly property var fields: item.data || []

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.sm

    Tk.Overline { title: qsTr("SHAPE DATA") }
    Tk.Caption {
        visible: section.fields.length === 0
        text: qsTr("No data yet. Add fields such as Owner, Phone or IP address; Find searches them too.")
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: section.fields
        delegate: Tk.PropertyRow {
            id: row
            required property var modelData
            required property int index
            objectName: "diagramDataRow_" + index
            label: modelData.key
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.TextField {
                    objectName: "diagramDataValue"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    small: true
                    text: row.modelData.value
                    tooltip: qsTr("Value of %1").arg(row.modelData.key)
                    onEditingFinished: if (text !== row.modelData.value)
                        section.diagram.setData(section.item.id, row.modelData.key, text)
                }
                Tk.IconButton {
                    objectName: "diagramDataRemove"
                    iconName: "x"
                    small: true
                    tooltip: qsTr("Remove %1").arg(row.modelData.key)
                    onClicked: section.diagram.removeData(section.item.id, row.modelData.key)
                }
            }
        }
    }
    Tk.Divider {}
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.TextField {
            id: newKey
            objectName: "diagramDataNewKey"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            small: true
            placeholderText: qsTr("Field")
            tooltip: qsTr("Name of a new data field")
        }
        Tk.TextField {
            id: newValue
            objectName: "diagramDataNewValue"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            small: true
            placeholderText: qsTr("Value")
            tooltip: qsTr("Value of the new field")
            onAccepted: addButton.clicked()
        }
        Tk.IconButton {
            id: addButton
            objectName: "diagramDataAdd"
            iconName: "plus"
            small: true
            enabled: newKey.text.trim().length > 0
            tooltip: qsTr("Add the field")
            onClicked: {
                if (section.diagram.setData(section.item.id, newKey.text, newValue.text)) {
                    newKey.text = ""
                    newValue.text = ""
                }
            }
        }
    }
}
