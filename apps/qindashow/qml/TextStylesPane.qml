// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Show

// Slide Master ▸ Text Styles (M75, D-084): the current master's title
// style and body levels 1–5 — font (the theme's heading and body fonts
// first, written as PowerPoint's +mj-lt / +mn-lt), size, bold, italic,
// underline, colour, bullet, alignment, indent. Each change is one undo
// step through ShowMasterView.setStyleLevel; every slide that inherits the
// level follows (the deck re-resolves).
Tk.Scroll {
    id: pane
    required property ShowHost showHost
    readonly property ShowMasterView view: showHost.masterView
    readonly property var fonts: {
        const out = view.themeFonts.slice()
        for (const family of showHost.fontFamilies) if (out.indexOf(family) < 0) out.push(family)
        return out
    }
    readonly property var bullets: ["", "•", "–", "▪", "◦", "➢", "1."]
    readonly property var alignments: [
        { iconName: "text-align-start", value: "left", tooltip: qsTr("Align left") },
        { iconName: "text-align-center", value: "center", tooltip: qsTr("Center") },
        { iconName: "text-align-end", value: "right", tooltip: qsTr("Align right") },
        { iconName: "text-align-justify", value: "justify", tooltip: qsTr("Justify") }]
    property int colorRow: -1

    objectName: "textStylesPane"

    function set(row, key, value) {
        const look = {}
        look[key] = value
        pane.view.setStyleLevel(row, look)
    }
    function alignmentIndex(name) {
        for (let i = 0; i < alignments.length; ++i) if (alignments[i].value === name) return i
        return 0
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        width: pane.width

        Tk.Caption {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            wrapMode: Text.Wrap
            text: qsTr("The slide master's text: every slide and layout that does not set its own follows it.")
        }
        Repeater {
            model: pane.view.active ? pane.view.styleLevels : []
            Tk.PropertyGroup {
                id: group
                required property var modelData
                readonly property int row: modelData.row
                readonly property bool isTitle: row === 0
                objectName: "textStyleGroup_" + row
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                title: modelData.label
                collapsed: row > 2
                Tk.PropertyRow {
                    label: qsTr("Font")
                    dense: true
                    Tk.ComboBox {
                        objectName: "textStyleFont_" + group.row
                        small: true
                        model: pane.fonts
                        currentIndex: Math.max(0, pane.fonts.indexOf(group.modelData.family))
                        onActivated: function(index) { pane.set(group.row, "family", pane.fonts[index]) }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Size")
                    dense: true
                    Tk.NumberField {
                        objectName: "textStyleSize_" + group.row
                        small: true
                        from: 1
                        to: 400
                        decimals: 1
                        scrub: false
                        suffix: qsTr(" pt")
                        value: group.modelData.size
                        onValueModified: function(value) { pane.set(group.row, "size", value) }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Style")
                    dense: true
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        Tk.CheckBox {
                            objectName: "textStyleBold_" + group.row
                            small: true
                            text: qsTr("Bold")
                            checked: group.modelData.bold
                            onToggled: pane.set(group.row, "bold", checked)
                        }
                        Tk.CheckBox {
                            objectName: "textStyleItalic_" + group.row
                            small: true
                            text: qsTr("Italic")
                            checked: group.modelData.italic
                            onToggled: pane.set(group.row, "italic", checked)
                        }
                        Tk.CheckBox {
                            objectName: "textStyleUnderline_" + group.row
                            small: true
                            text: qsTr("Underline")
                            checked: group.modelData.underline
                            onToggled: pane.set(group.row, "underline", checked)
                        }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Colour")
                    dense: true
                    Tk.ColorField {
                        objectName: "textStyleColor_" + group.row
                        small: true
                        color: group.modelData.color
                        onColorEdited: function(color) { pane.set(group.row, "color", color) }
                        onPickRequested: {
                            pane.colorRow = group.row
                            colorDialog.selectedColor = group.modelData.color
                            colorDialog.open()
                        }
                    }
                }
                Tk.PropertyRow {
                    visible: !group.isTitle
                    label: qsTr("Bullet")
                    dense: true
                    Tk.ComboBox {
                        objectName: "textStyleBullet_" + group.row
                        small: true
                        model: pane.bullets.map(function(b) { return b.length > 0 ? b : qsTr("None") })
                        currentIndex: Math.max(0, pane.bullets.indexOf(group.modelData.bullet))
                        onActivated: function(index) { pane.set(group.row, "bullet", pane.bullets[index]) }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Alignment")
                    dense: true
                    Tk.Segmented {
                        objectName: "textStyleAlignment_" + group.row
                        small: true
                        model: pane.alignments
                        currentIndex: pane.alignmentIndex(group.modelData.alignment)
                        onActivated: function(index) { pane.set(group.row, "alignment", pane.alignments[index].value) }
                    }
                }
                Tk.PropertyRow {
                    visible: !group.isTitle
                    label: qsTr("Indent")
                    dense: true
                    Tk.NumberField {
                        objectName: "textStyleIndent_" + group.row
                        small: true
                        from: 0
                        to: 400
                        decimals: 1
                        scrub: false
                        suffix: qsTr(" pt")
                        value: group.modelData.indent
                        onValueModified: function(value) { pane.set(group.row, "indent", value) }
                    }
                }
            }
        }
        Tk.EmptyState {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: !pane.view.active
            iconName: "type"
            title: qsTr("Slide Master view only")
            text: qsTr("Open View ▸ Slide Master to edit the master's text styles.")
        }
    }
    Dialogs.ColorDialog {
        id: colorDialog
        title: qsTr("Text colour")
        onAccepted: if (pane.colorRow >= 0) pane.set(pane.colorRow, "color", selectedColor)
    }
}
