// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Home ▸ Multilevel List ▸ Define New Multilevel List (M73, D-082): Word's
// dialog, level by level — the number style (or a bullet), the format text
// with the levels it includes ("%1.%2."), the start, which level restarts
// it, legal-style numbering, the number's alignment and position, the text
// indent and what follows the number (tab, space, nothing). The preview
// draws all nine levels. Answered through ListController (write_lists.h):
// the caret's list takes the definition, other paragraphs become a new
// list. Positions are inches (the host speaks twips).
Tk.Dialog {
    id: dialog
    required property QtObject lists
    property var levels: []
    property int level: 0
    property var formats: []
    property bool loading: false
    readonly property var current: levels.length > level ? levels[level] : ({})
    readonly property var suffixes: ["tab", "space", "nothing"]
    readonly property var justifications: ["left", "center", "right"]
    // The preview indents each level by its number position, to scale.
    readonly property real previewPerInch: Tk.Theme.space.lg * 3
    objectName: "defineMultilevelDialog"
    title: qsTr("Define New Multilevel List")
    dialogWidth: 560
    primaryText: qsTr("OK")

    function load(info) {
        loading = true
        formats = dialog.lists.numberFormats()
        levels = info.levels.map(function(l) { return Object.assign({}, l) })
        level = info.level
        show()
        loading = false
    }
    // The selected level's values into the fields.
    function show() {
        const l = current
        loading = true
        style.currentIndex = Math.max(0, formats.findIndex(function(f) { return f.format === l.format }))
        formatText.text = l.text
        startField.value = l.start
        restart.currentIndex = l.restartAfter === -2 ? 0 : l.restartAfter === -1 ? 1 : l.restartAfter + 2
        legal.checked = l.legal
        alignment.currentIndex = Math.max(0, justifications.indexOf(l.justification))
        atField.value = l.numberPosition / 1440
        indentField.value = l.textIndent / 1440
        follow.currentIndex = Math.max(0, suffixes.indexOf(l.suffix))
        loading = false
    }
    function edit(key, value) {
        if (loading) return
        const next = levels.slice()
        next[level] = Object.assign({}, next[level])
        next[level][key] = value
        levels = next
    }
    function insertLevel(k) {
        formatText.insert(formatText.cursorPosition, "%" + (k + 1))
        edit("text", formatText.text)
    }
    onAccepted: lists.applyMultilevelDefinition({ levels: levels })

    Connections {
        target: dialog.lists
        function onDefineMultilevelRequested() { dialog.load(dialog.lists.multilevelInfo()); dialog.open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Level")
            Tk.Segmented {
                objectName: "multilevelLevel"
                model: ["1", "2", "3", "4", "5", "6", "7", "8", "9"]
                currentIndex: dialog.level
                onActivated: function(index) { dialog.level = index; dialog.show() }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number style")
            Tk.ComboBox {
                id: style
                objectName: "multilevelStyle"
                implicitWidth: 220
                model: dialog.formats.map(function(f) { return f.label })
                onActivated: function(index) { dialog.edit("format", dialog.formats[index].format) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number format")
            hint: qsTr("%1 … %9 stand for the numbers of levels 1 to 9.")
            Tk.TextField {
                id: formatText
                objectName: "multilevelText"
                onTextEdited: dialog.edit("text", formatText.text)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Include level")
            Tk.Flex {
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                Repeater {
                    model: dialog.level + 1
                    Tk.Button {
                        objectName: "multilevelInclude_" + index
                        small: true
                        text: qsTr("Level %1").arg(index + 1)
                        onClicked: dialog.insertLevel(index)
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Start at")
            Tk.NumberField {
                id: startField
                objectName: "multilevelStart"
                from: 0; to: 32767; stepSize: 1; decimals: 0
                onValueModified: function(v) { value = v; dialog.edit("start", v) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Restart after")
            Tk.ComboBox {
                id: restart
                objectName: "multilevelRestart"
                model: [qsTr("Any higher level"), qsTr("Never")].concat(
                    [1, 2, 3, 4, 5, 6, 7, 8].slice(0, Math.max(0, dialog.level)).map(function(n) { return qsTr("Level %1").arg(n) }))
                onActivated: function(index) { dialog.edit("restartAfter", index === 0 ? -2 : index === 1 ? -1 : index - 2) }
            }
        }
        Tk.CheckBox {
            id: legal
            objectName: "multilevelLegal"
            text: qsTr("Legal style numbering (levels above in 1, 2, 3)")
            onToggled: dialog.edit("legal", checked)
        }
        Tk.PropertyRow {
            label: qsTr("Number alignment")
            Tk.Segmented {
                id: alignment
                objectName: "multilevelAlignment"
                model: [qsTr("Left"), qsTr("Centered"), qsTr("Right")]
                onActivated: function(index) { dialog.edit("justification", dialog.justifications[index]) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Aligned at")
            Tk.NumberField {
                id: atField
                objectName: "multilevelAt"
                from: -10; to: 22; stepSize: 0.05; decimals: 2; suffix: qsTr(" in")
                onValueModified: function(v) { value = v; dialog.edit("numberPosition", Math.round(v * 1440)) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Text indent at")
            Tk.NumberField {
                id: indentField
                objectName: "multilevelIndent"
                from: -10; to: 22; stepSize: 0.05; decimals: 2; suffix: qsTr(" in")
                onValueModified: function(v) { value = v; dialog.edit("textIndent", Math.round(v * 1440)) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Follow number with")
            Tk.ComboBox {
                id: follow
                objectName: "multilevelFollow"
                model: [qsTr("Tab character"), qsTr("Space"), qsTr("Nothing")]
                onActivated: function(index) { dialog.edit("suffix", dialog.suffixes[index]) }
            }
        }
        Tk.Overline { text: qsTr("Preview") }
        Tk.Box {
            objectName: "multilevelPreview"
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                Repeater {
                    model: dialog.levels.length
                    Tk.Box {
                        paddingLeft: Math.max(0, dialog.levels[index].numberPosition / 1440 * dialog.previewPerInch)
                        Tk.Flex {
                            gap: Tk.Theme.space.sm
                            Tk.Label {
                                objectName: "multilevelPreview_" + index
                                text: dialog.lists.previewLabel(dialog.levels, index, 1)
                                accent: index === dialog.level
                            }
                            Tk.Label { text: qsTr("Level %1").arg(index + 1); muted: index !== dialog.level }
                        }
                    }
                }
            }
        }
    }
}
