// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The categories of an item being edited (M81, D-090): the chosen ones as
// chips with their colour, and a Categorize button whose menu lists the
// account's master list to add or remove one. Nothing is stored until the
// dialog that holds it saves `names`.
Tk.Flex {
    id: field

    property MailService service: null
    property string accountId: "local"
    property var names: []
    property bool readOnly: false
    readonly property var categories: service !== null && service.organizer.revision >= 0 ? service.organizer.categoriesFor(accountId) : []

    direction: Tk.Flex.Row
    wrap: Tk.Flex.Wrap
    align: Tk.Flex.Center
    gap: Tk.Theme.space.xs

    function has(name) { return names.some(function(n) { return String(n).toLowerCase() === String(name).toLowerCase() }) }
    function toggle(name) {
        names = has(name) ? names.filter(function(n) { return String(n).toLowerCase() !== String(name).toLowerCase() })
                          : names.concat([name])
    }
    function colorOf(name) {
        const entry = categories.find(function(c) { return String(c.name).toLowerCase() === String(name).toLowerCase() })
        return entry ? String(entry.color || "") : ""
    }

    Repeater {
        model: field.names
        delegate: Tk.Flex {
            id: chip
            required property var modelData
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            // The category's colour is user data (AGENTS.md rule 3).
            Rectangle {
                implicitWidth: Tk.Theme.space.md
                implicitHeight: implicitWidth
                radius: width / 2
                color: field.colorOf(chip.modelData).length > 0 ? field.colorOf(chip.modelData) : Tk.Theme.color.textMuted
            }
            Tk.Chip {
                objectName: field.objectName + "_" + String(chip.modelData)
                small: true
                text: String(chip.modelData)
                iconName: field.readOnly ? "" : "x"
                tooltip: field.readOnly ? String(chip.modelData) : qsTr("Remove %1").arg(String(chip.modelData))
                enabled: !field.readOnly
                onClicked: field.toggle(String(chip.modelData))
            }
        }
    }
    Tk.Button {
        objectName: field.objectName + "Pick"
        visible: !field.readOnly
        small: true
        variant: "ghost"
        iconName: "tag"
        text: field.names.length === 0 ? qsTr("Categorize") : ""
        tooltip: qsTr("Choose categories")
        onClicked: pickMenu.popup()
    }
    Tk.Menu {
        id: pickMenu
        Repeater {
            model: field.categories
            delegate: Tk.MenuItem {
                required property var modelData
                text: String(modelData.name)
                checkable: true
                checked: field.has(modelData.name)
                onTriggered: field.toggle(String(modelData.name))
            }
        }
    }
}
