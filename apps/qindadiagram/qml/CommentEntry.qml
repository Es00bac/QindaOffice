// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One comment or reply in QindaDiagram's Comments pane (M87, D-095):
// author, date, a Resolved badge and its text — or, while editing it, a
// field with Save and Cancel. QindaShow's CommentEntry.qml is the model
// (each app's QML module keeps its own copy).
Tk.Flex {
    id: root

    required property var pane
    required property var entry
    property bool reply: false
    readonly property bool editing: root.pane.editing === root.entry.id

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.Flex {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.Label { text: root.entry.author.length > 0 ? root.entry.author : qsTr("Unknown"); font.bold: true }
        Tk.Caption { text: root.entry.date }
        Tk.Badge { visible: root.entry.done && !root.reply; text: qsTr("Resolved") }
        Item { Tk.Flex.grow: 1; implicitHeight: 1 }
        Tk.IconButton {
            objectName: "commentEdit"
            small: true
            iconName: "pencil"
            tooltip: qsTr("Edit")
            visible: !root.editing
            onClicked: { root.pane.editing = root.entry.id; field.text = root.entry.text; field.forceActiveFocus() }
        }
    }
    Tk.Label {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: !root.editing
        wrapMode: Text.Wrap
        text: root.entry.text
    }
    Tk.TextArea {
        id: field
        objectName: "commentEditText"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: root.editing
        rows: 2
    }
    Tk.Flex {
        visible: root.editing
        gap: Tk.Theme.space.xs
        Tk.Button {
            small: true
            text: qsTr("Save")
            enabled: field.text.trim().length > 0
            onClicked: { root.pane.diagram.editComment(root.entry.id, field.text); root.pane.editing = "" }
        }
        Tk.Button { small: true; text: qsTr("Cancel"); onClicked: root.pane.editing = "" }
    }
}
