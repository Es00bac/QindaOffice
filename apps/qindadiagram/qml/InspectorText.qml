// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The Text section of the inspector (D-050): the selected item's text and
// its one-run formatting — face, size, bold/italic/underline, colour and
// the box alignment. Faces and colours are document content (the diagram's
// own), which is why they may differ from the chrome's.
Tk.Flex {
    id: section
    objectName: "diagramInspectorText"

    required property DiagramService diagram
    readonly property var item: diagram.selected
    readonly property var text: item.textStyle || ({})
    readonly property var faces: {
        const list = ["Inter", "IBM Plex Sans", "Lato", "DejaVu Sans", "DejaVu Serif", "Liberation Mono"]
        if (section.text.family && list.indexOf(section.text.family) < 0) list.unshift(section.text.family)
        return list
    }
    readonly property var themeSwatches: {
        const list = diagram.themes
        for (let i = 0; i < list.length; ++i) if (list[i].id === diagram.themeId) return Array.from(list[i].swatches)
        return []
    }

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.sm

    Tk.Overline { title: qsTr("TEXT") }
    Tk.TextArea {
        id: body
        objectName: "diagramTextBody"
        rows: 3
        text: section.item.text || ""
        placeholderText: qsTr("Type the shape's text")
        tooltip: qsTr("Text of the selected item (stored when you leave the field)")
        onEditingFinished: if (text !== (section.item.text || "")) section.diagram.setText(section.item.id, text)
    }
    Tk.PropertyRow {
        label: qsTr("Font")
        Tk.ComboBox {
            objectName: "diagramFontFace"
            model: section.faces
            currentIndex: Math.max(0, section.faces.indexOf(section.text.family || "Inter"))
            tooltip: qsTr("Typeface of the text")
            onActivated: function(index) { section.diagram.setTextStyle({ family: section.faces[index] }) }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Size")
        Tk.NumberField {
            objectName: "diagramFontSize"
            value: section.text.size || 11
            from: 4
            to: 200
            stepSize: 1
            decimals: 1
            suffix: " pt"
            onValueModified: function(value) { section.diagram.setTextStyle({ size: value }) }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Style")
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.IconButton {
                objectName: "diagramTextBold"
                iconName: "bold"
                checkable: true
                checked: !!section.text.bold
                tooltip: qsTr("Bold")
                onClicked: section.diagram.setTextStyle({ bold: checked })
            }
            Tk.IconButton {
                objectName: "diagramTextItalic"
                iconName: "italic"
                checkable: true
                checked: !!section.text.italic
                tooltip: qsTr("Italic")
                onClicked: section.diagram.setTextStyle({ italic: checked })
            }
            Tk.IconButton {
                objectName: "diagramTextUnderline"
                iconName: "underline"
                checkable: true
                checked: !!section.text.underline
                tooltip: qsTr("Underline")
                onClicked: section.diagram.setTextStyle({ underline: checked })
            }
            Tk.ColorButton {
                objectName: "diagramTextColor"
                small: true
                color: section.text.color || "#1f2937"
                colors: section.themeSwatches.concat(["#1f2937", "#475569", "#ffffff"])
                tooltip: qsTr("Text colour")
                onColorSelected: function(color) { section.diagram.setTextStyle({ color: String(color) }) }
            }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Across")
        Tk.Segmented {
            objectName: "diagramTextAlign"
            stretch: true
            model: [{ iconName: "text-align-start", tooltip: qsTr("Left") }, { iconName: "text-align-center", tooltip: qsTr("Centre") },
                    { iconName: "text-align-end", tooltip: qsTr("Right") }]
            currentIndex: ["left", "center", "right"].indexOf(section.text.align || "center")
            tooltip: qsTr("Horizontal text alignment")
            onActivated: function(index) { section.diagram.setTextStyle({ align: ["left", "center", "right"][index] }) }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Down")
        Tk.Segmented {
            objectName: "diagramTextVAlign"
            stretch: true
            model: [{ iconName: "arrow-up-to-line", tooltip: qsTr("Top") },
                    { iconName: "align-vertical-justify-center", tooltip: qsTr("Middle") },
                    { iconName: "arrow-down-to-line", tooltip: qsTr("Bottom") }]
            currentIndex: ["top", "middle", "bottom"].indexOf(section.text.valign || "middle")
            tooltip: qsTr("Vertical text alignment")
            onActivated: function(index) { section.diagram.setTextStyle({ valign: ["top", "middle", "bottom"][index] }) }
        }
    }
}
