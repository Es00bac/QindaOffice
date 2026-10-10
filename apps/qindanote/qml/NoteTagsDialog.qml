// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Tags ▸ Apply Tag… and Customize Tags… (M84, D-093): every tag — the
// built-in ones and the user's own — to apply to the selected lines, and
// the user's custom tags to add (a name, an icon and a colour) or remove.
// A custom tag's look is saved with each note that has it, so removing it
// here leaves those notes as they are.
Tk.Dialog {
    id: dialog
    required property NoteHost note
    property bool customize: false
    property color newColor: "#1a5fb4"
    objectName: "noteTagsDialog"
    title: dialog.customize ? qsTr("Customize Tags") : qsTr("Tags")
    dialogWidth: 420
    primaryText: qsTr("Close")
    secondaryText: ""
    onOpened: if (dialog.customize) nameField.forceActiveFocus(Qt.TabFocusReason)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Scroll {
            id: scroll
            objectName: "noteTagsList"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            implicitHeight: 260
            overflowX: Tk.Scroll.Hidden
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                width: scroll.width - Tk.Theme.space.sm
                Repeater {
                    model: dialog.note.tagCatalog
                    Tk.ListRow {
                        required property var modelData
                        objectName: "noteTagRow_" + modelData.id
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        small: true
                        text: modelData.name
                        secondaryText: modelData.custom ? qsTr("Custom tag") : ""
                        leading: Tk.Icon { name: modelData.icon; color: modelData.color; size: Tk.Theme.size.iconSm }
                        trailing: Tk.IconButton {
                            visible: modelData.custom
                            small: true
                            iconName: "trash-2"
                            tooltip: qsTr("Remove this custom tag")
                            onClicked: dialog.note.removeCustomTag(modelData.id)
                        }
                        onClicked: { dialog.note.applyTag(modelData.id); dialog.close() }
                    }
                }
            }
        }
        Tk.Overline { title: qsTr("New custom tag") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.TextField { id: nameField; objectName: "noteTagName"; Tk.Flex.grow: 1; placeholderText: qsTr("Name, e.g. Waiting for") }
            Tk.ComboBox {
                id: iconBox
                objectName: "noteTagIcon"
                small: true
                implicitWidth: 120
                model: dialog.note.customTagIcons
            }
            Tk.ColorButton {
                objectName: "noteTagColor"
                small: true
                color: dialog.newColor
                tooltip: qsTr("Tag colour")
                colors: ["#1a5fb4", "#26a269", "#c01c28", "#e66100", "#d89b00", "#813d9c", "#595959"]
                onColorSelected: function(c) { dialog.newColor = c }
            }
            Tk.Button {
                objectName: "noteTagAdd"
                small: true
                iconName: "plus"
                text: qsTr("Add")
                available: nameField.text.trim().length > 0
                onClicked: {
                    if (dialog.note.addCustomTag(nameField.text, iconBox.currentText, dialog.newColor)) nameField.text = ""
                }
            }
        }
    }
}
