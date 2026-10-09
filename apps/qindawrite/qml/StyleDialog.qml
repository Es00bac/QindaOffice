// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// New Style / Modify Style (M73, D-082): Word's "Create New Style from
// Formatting" and "Modify Style" dialog — name, type (paragraph, character,
// linked), based on, style for following paragraph, the quick formatting
// row (font, size, bold, italic, underline, colour, alignment), Format ▸
// Font…, Paragraph…, Tabs…, Borders… (M70's own dialogs, on the style's
// sample: formatRequested) and Numbering, "Add to the Styles gallery", a
// preview and Word's description. OK writes the style and re-derives every
// paragraph that uses it (StylesController::commitDraft, one undo step).
Tk.Dialog {
    id: dialog
    required property QtObject styles
    property var families: []
    readonly property QtObject draft: styles.draft
    readonly property var look: draft.look
    readonly property var types: ["paragraph", "character", "linked"]
    readonly property var alignments: ["left", "center", "right", "justify"]
    readonly property var numberings: ["", "none", "bullet", "number"]
    readonly property var baseList: draft.type === "character" ? draft.characterStyles : draft.paragraphStyles
    signal formatRequested(string kind)
    objectName: "styleDialog"
    title: draft.mode === "new" ? qsTr("Create New Style from Formatting") : qsTr("Modify Style")
    dialogWidth: 560
    primaryText: qsTr("OK")
    primaryEnabled: draft.name.trim().length > 0

    function setFont(key, value) {
        const answer = { changed: [key] }
        answer[key] = value
        draft.applyFont(answer)
    }
    function paragraph(key, value) {
        const answer = { changed: [key] }
        answer[key] = value
        draft.applyParagraph(answer)
    }
    function indexOfId(list, id, offset) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return i + offset
        return 0
    }
    onAccepted: styles.commitDraft()

    Connections {
        target: dialog.styles
        function onStyleDialogRequested() { dialog.open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyGroup {
            title: qsTr("Properties")
            collapsible: false
            Tk.PropertyRow {
                label: qsTr("Name")
                Tk.TextField {
                    objectName: "styleName"
                    text: dialog.draft.name
                    onTextEdited: dialog.draft.name = text
                }
            }
            Tk.PropertyRow {
                label: qsTr("Style type")
                Tk.ComboBox {
                    objectName: "styleType"
                    enabled: dialog.draft.mode === "new"
                    model: [qsTr("Paragraph"), qsTr("Character"), qsTr("Linked (paragraph and character)")]
                    currentIndex: Math.max(0, dialog.types.indexOf(dialog.draft.type))
                    onActivated: function(index) { dialog.draft.type = dialog.types[index] }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Style based on")
                Tk.ComboBox {
                    objectName: "styleBasedOn"
                    model: [qsTr("(no style)")].concat(dialog.baseList.map(function(s) { return s.name }))
                    currentIndex: dialog.indexOfId(dialog.baseList, dialog.draft.basedOn, 1)
                    onActivated: function(index) { dialog.draft.basedOn = index === 0 ? "" : dialog.baseList[index - 1].id }
                }
            }
            Tk.PropertyRow {
                // Word's "Style for following paragraph", short enough for the label column.
                label: qsTr("Next paragraph")
                visible: dialog.draft.type !== "character"
                Tk.ComboBox {
                    objectName: "styleNext"
                    model: [qsTr("(this style)")].concat(dialog.draft.paragraphStyles.map(function(s) { return s.name }))
                    currentIndex: dialog.indexOfId(dialog.draft.paragraphStyles, dialog.draft.next, 1)
                    onActivated: function(index) { dialog.draft.next = index === 0 ? "" : dialog.draft.paragraphStyles[index - 1].id }
                }
            }
        }
        Tk.PropertyGroup {
            title: qsTr("Formatting")
            collapsible: false
            Tk.Flex {
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                Tk.ComboBox {
                    id: fontCombo
                    // A family the system lacks (Calibri on Linux) is still the style's.
                    readonly property var choices: dialog.families.indexOf(dialog.look.family) >= 0
                                                   ? dialog.families : [dialog.look.family].concat(dialog.families)
                    objectName: "styleFont"
                    implicitWidth: 170
                    model: choices
                    currentIndex: Math.max(0, choices.indexOf(dialog.look.family))
                    onActivated: function(index) { dialog.setFont("family", fontCombo.choices[index]) }
                }
                Tk.NumberField {
                    objectName: "styleSize"
                    from: 1; to: 400; stepSize: 1; decimals: 1; suffix: qsTr(" pt")
                    value: dialog.look.size
                    onValueModified: function(v) { dialog.setFont("size", v) }
                }
                // The state shows in the icon's colour: a checkable button's own
                // click would break its binding to the style's look.
                Tk.IconButton {
                    objectName: "styleBold"; iconName: "bold"; tooltip: qsTr("Bold")
                    iconColor: dialog.look.bold ? Tk.Theme.color.accent : Tk.Theme.color.text
                    onClicked: dialog.setFont("bold", !dialog.look.bold)
                }
                Tk.IconButton {
                    objectName: "styleItalic"; iconName: "italic"; tooltip: qsTr("Italic")
                    iconColor: dialog.look.italic ? Tk.Theme.color.accent : Tk.Theme.color.text
                    onClicked: dialog.setFont("italic", !dialog.look.italic)
                }
                Tk.IconButton {
                    objectName: "styleUnderline"; iconName: "underline"; tooltip: qsTr("Underline")
                    iconColor: dialog.look.underline ? Tk.Theme.color.accent : Tk.Theme.color.text
                    onClicked: dialog.setFont("underline", dialog.look.underline ? "none" : "single")
                }
                Tk.ColorButton {
                    objectName: "styleColor"
                    tooltip: qsTr("Font colour")
                    color: dialog.look.color.length > 0 ? dialog.look.color : Tk.Theme.color.text
                    colors: ["#000000", "#404040", "#1f4e79", "#2f5496", "#4472c4", "#c00000", "#ed7d31", "#70ad47"]
                    onColorSelected: function(c) { dialog.setFont("color", String(c)) }
                }
                Tk.Segmented {
                    objectName: "styleAlignment"
                    visible: dialog.draft.type !== "character"
                    model: [qsTr("Left"), qsTr("Center"), qsTr("Right"), qsTr("Justify")]
                    currentIndex: Math.max(0, dialog.alignments.indexOf(dialog.look.alignment))
                    onActivated: function(index) { dialog.paragraph("alignment", dialog.alignments[index]) }
                }
            }
            Tk.Flex {
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                Tk.Button { objectName: "styleFontButton"; small: true; text: qsTr("Font…"); onClicked: dialog.formatRequested("font") }
                Tk.Button {
                    objectName: "styleParagraphButton"; small: true; text: qsTr("Paragraph…")
                    enabled: dialog.draft.type !== "character"; onClicked: dialog.formatRequested("paragraph")
                }
                Tk.Button {
                    objectName: "styleTabsButton"; small: true; text: qsTr("Tabs…")
                    enabled: dialog.draft.type !== "character"; onClicked: dialog.formatRequested("tabs")
                }
                Tk.Button {
                    objectName: "styleBordersButton"; small: true; text: qsTr("Borders…")
                    enabled: dialog.draft.type !== "character"; onClicked: dialog.formatRequested("borders")
                }
                Tk.ComboBox {
                    objectName: "styleNumbering"
                    implicitWidth: 170
                    enabled: dialog.draft.mode === "modify" && dialog.draft.type !== "character"
                    tooltip: qsTr("Numbering")
                    model: [qsTr("Numbering: as it is"), qsTr("No numbering"), qsTr("Bullets"), qsTr("Numbers 1. 2. 3.")]
                    currentIndex: Math.max(0, dialog.numberings.indexOf(dialog.draft.numbering))
                    onActivated: function(index) { dialog.draft.numbering = dialog.numberings[index] }
                }
            }
            Tk.CheckBox {
                objectName: "styleGallery"
                text: qsTr("Add to the Styles gallery")
                checked: dialog.draft.quick
                onToggled: dialog.draft.quick = checked
            }
        }
        Tk.Box {
            objectName: "stylePreviewBox"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.md
            Tk.Label {
                objectName: "styleSample"
                // The style's look is the document's content, not chrome.
                text: qsTr("Sample Text Sample Text Sample Text")
                horizontalAlignment: dialog.look.alignment === "center" ? Text.AlignHCenter
                                   : dialog.look.alignment === "right" ? Text.AlignRight : Text.AlignLeft
                color: dialog.look.color.length > 0 ? dialog.look.color : Tk.Theme.color.text
                font.family: dialog.look.family
                font.pixelSize: Math.max(1, dialog.look.size * 96 / 72)
                font.bold: dialog.look.bold
                font.italic: dialog.look.italic
                font.underline: dialog.look.underline
            }
        }
        Tk.Caption {
            objectName: "styleDescription"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            wrapMode: Text.Wrap
            text: dialog.draft.description
        }
    }
}
