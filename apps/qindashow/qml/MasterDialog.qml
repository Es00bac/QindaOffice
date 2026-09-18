// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The slide-master editor: a list of masters plus the four things a master
// decides — background, title colour, body colour and font. Apply hands
// the whole list back (one undo step for the deck).
Tk.Dialog {
    id: dialog
    property var fontFamilies: []
    property var masters: []
    property int current: -1
    signal mastersApplied(var masters)
    objectName: "showMasterDialog"
    title: qsTr("Slide Masters")
    dialogWidth: 560
    primaryText: qsTr("Apply")
    function load(list) {
        dialog.masters = list.map(function(m) { return Object.assign({}, m) })
        dialog.current = dialog.masters.length > 0 ? 0 : -1
        dialog.refresh()
    }
    function refresh() {
        const m = dialog.current >= 0 ? dialog.masters[dialog.current] : null
        nameField.text = m !== null ? m.name : ""
        background.color = m !== null ? m.background : "white"
        titleColor.color = m !== null ? m.titleColor : "black"
        bodyColor.color = m !== null ? m.bodyColor : "black"
        fontCombo.currentIndex = m !== null ? Math.max(0, dialog.fontFamilies.indexOf(m.fontFamily)) : 0
        list.model = dialog.masters.length
    }
    function set(field, value) {
        if (dialog.current < 0) return
        const copy = dialog.masters.slice()
        copy[dialog.current] = Object.assign({}, copy[dialog.current])
        copy[dialog.current][field] = value
        dialog.masters = copy
        list.model = dialog.masters.length
    }
    onAccepted: dialog.mastersApplied(dialog.masters)
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex.basis: 180
            Tk.Scroll {
                implicitHeight: 180
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Repeater {
                        id: list
                        model: 0
                        Tk.ListRow {
                            required property int index
                            objectName: "showMasterRow_" + index
                            text: dialog.masters[index].name.length > 0 ? dialog.masters[index].name : dialog.masters[index].id
                            selected: index === dialog.current
                            onClicked: { dialog.current = index; dialog.refresh() }
                        }
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.Button {
                    objectName: "showMasterAdd"; text: qsTr("Add"); small: true
                    onClicked: {
                        const n = dialog.masters.length + 1
                        dialog.masters = dialog.masters.concat([{ id: "", name: qsTr("Master %1").arg(n), background: "#ffffff", titleColor: "#000000", bodyColor: "#000000", fontFamily: "" }])
                        dialog.current = dialog.masters.length - 1
                        dialog.refresh()
                    }
                }
                Tk.Button {
                    objectName: "showMasterRemove"; text: qsTr("Remove"); small: true; available: dialog.current >= 0
                    onClicked: {
                        const copy = dialog.masters.slice(); copy.splice(dialog.current, 1)
                        dialog.masters = copy
                        dialog.current = dialog.masters.length > 0 ? 0 : -1
                        dialog.refresh()
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            enabled: dialog.current >= 0
            Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "showMasterName"; width: 200; onTextEdited: dialog.set("name", text) } }
            Tk.PropertyRow { label: qsTr("Background"); Tk.ColorButton { id: background; objectName: "showMasterBackground"; colors: ["#ffffff", "#f5f5f0", "#202028", "#14284f", "#1e3a2e", "#5b1e1e", "#fff3b0"]; onColorSelected: function(c) { dialog.set("background", c) } } }
            Tk.PropertyRow { label: qsTr("Title colour"); Tk.ColorButton { id: titleColor; objectName: "showMasterTitleColor"; colors: ["#000000", "#ffffff", "#1c62d4", "#d21c1c", "#1f9d3a", "#f28c19"]; onColorSelected: function(c) { dialog.set("titleColor", c) } } }
            Tk.PropertyRow { label: qsTr("Body colour"); Tk.ColorButton { id: bodyColor; objectName: "showMasterBodyColor"; colors: ["#000000", "#ffffff", "#333333", "#1c62d4", "#7f7f7f"]; onColorSelected: function(c) { dialog.set("bodyColor", c) } } }
            Tk.PropertyRow { label: qsTr("Font"); Tk.ComboBox { id: fontCombo; objectName: "showMasterFont"; implicitWidth: 200; model: dialog.fontFamilies; onActivated: function(index) { dialog.set("fontFamily", dialog.fontFamilies[index]) } } }
        }
    }
}
