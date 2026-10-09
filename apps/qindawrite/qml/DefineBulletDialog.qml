// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Home ▸ Bullets ▸ Define New Bullet (M73, D-082): any symbol — one of
// Word's own (kept in Word's font: Symbol, Wingdings, Courier New) or any
// character typed or pasted in any installed font — its colour and its
// alignment at the number position. Answered through ListController
// (write_lists.h): on a bulleted list it changes the list's bullet, on
// other paragraphs it makes a new list.
Tk.Dialog {
    id: dialog
    required property QtObject lists
    property var families: []
    property string glyph: ""
    property string bulletFont: ""
    property string shown: ""
    property string color: ""
    readonly property var justifications: ["left", "center", "right"]
    // Word's symbols as Word stores them (a symbol font's private-use code),
    // and how they look.
    readonly property var symbols: [
        { glyph: "", font: "Symbol", shown: "•" }, { glyph: "o", font: "Courier New", shown: "o" },
        { glyph: "", font: "Wingdings", shown: "▪" }, { glyph: "", font: "Wingdings", shown: "■" },
        { glyph: "", font: "Wingdings", shown: "◆" }, { glyph: "", font: "Wingdings", shown: "❖" },
        { glyph: "", font: "Wingdings", shown: "➢" }, { glyph: "", font: "Wingdings", shown: "✓" },
        { glyph: "", font: "Wingdings", shown: "●" }, { glyph: "", font: "Wingdings", shown: "❑" },
        { glyph: "", font: "Wingdings", shown: "➔" }, { glyph: "", font: "Wingdings", shown: "☑" },
        { glyph: "–", font: "", shown: "–" }, { glyph: "—", font: "", shown: "—" },
        { glyph: "○", font: "", shown: "○" }, { glyph: "►", font: "", shown: "►" },
        { glyph: "★", font: "", shown: "★" }, { glyph: "→", font: "", shown: "→" },
        { glyph: "♥", font: "", shown: "♥" }, { glyph: "☐", font: "", shown: "☐" }]
    objectName: "defineBulletDialog"
    title: qsTr("Define New Bullet")
    dialogWidth: 460
    primaryText: qsTr("OK")
    primaryEnabled: glyph.length > 0

    function load(info) {
        glyph = info.glyph; bulletFont = info.font; shown = info.shown; color = info.color
        character.text = ""
        fontCombo.currentIndex = Math.max(0, fontCombo.model.indexOf(bulletFont.length > 0 ? bulletFont : qsTr("(paragraph font)")))
        alignment.currentIndex = Math.max(0, justifications.indexOf(info.justification))
        colorButton.color = color.length > 0 ? color : "#000000"
    }
    function pick(symbol) {
        glyph = symbol.glyph; bulletFont = symbol.font; shown = symbol.shown
        fontCombo.currentIndex = Math.max(0, fontCombo.model.indexOf(bulletFont.length > 0 ? bulletFont : qsTr("(paragraph font)")))
    }
    onAccepted: lists.applyDefineBullet({ glyph: glyph, font: bulletFont, color: color,
                                         justification: justifications[alignment.currentIndex] })

    Connections {
        target: dialog.lists
        function onDefineBulletRequested() { dialog.load(dialog.lists.bulletInfo()); dialog.open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Symbol") }
        Tk.Flex {
            objectName: "bulletSymbols"
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: dialog.symbols
                Tk.Button {
                    objectName: "bulletSymbol_" + index
                    small: true
                    text: modelData.shown
                    checkable: true
                    checked: dialog.glyph === modelData.glyph && dialog.bulletFont === modelData.font
                    tooltip: modelData.font.length > 0 ? modelData.font : qsTr("Paragraph font")
                    onClicked: dialog.pick(modelData)
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Character")
            Tk.TextField {
                id: character
                objectName: "bulletCharacter"
                placeholderText: qsTr("Type or paste any character")
                onTextEdited: if (text.length > 0) { dialog.glyph = text.slice(0, text.codePointAt(0) > 0xFFFF ? 2 : 1); dialog.shown = dialog.glyph }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Font")
            Tk.ComboBox {
                id: fontCombo
                objectName: "bulletFont"
                implicitWidth: 220
                model: [qsTr("(paragraph font)"), "Symbol", "Wingdings", "Courier New"].concat(dialog.families)
                onActivated: function(index) { dialog.bulletFont = index === 0 ? "" : model[index] }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Colour")
            Tk.ColorButton {
                id: colorButton
                objectName: "bulletColor"
                tooltip: qsTr("Bullet colour")
                colors: ["#000000", "#7f7f7f", "#1f4e79", "#4472c4", "#c00000", "#ed7d31", "#70ad47", "#7030a0"]
                onColorSelected: function(c) { dialog.color = String(c) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Alignment")
            Tk.Segmented {
                id: alignment
                objectName: "bulletAlignment"
                model: [qsTr("Left"), qsTr("Centered"), qsTr("Right")]
            }
        }
        Tk.Overline { text: qsTr("Preview") }
        Tk.Box {
            objectName: "bulletPreview"
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: [qsTr("First item"), qsTr("Second item"), qsTr("Third item")]
                    Tk.Flex {
                        gap: Tk.Theme.space.md
                        Tk.Label { text: dialog.shown; color: dialog.color.length > 0 ? dialog.color : Tk.Theme.color.text }
                        Tk.Label { text: modelData }
                    }
                }
            }
        }
    }
}
