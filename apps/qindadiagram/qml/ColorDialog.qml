// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The colour chooser the Fill / Line / Text Color and Page Background
// commands open (D-050): the drawing theme's colours first, a few neutrals,
// "none" where a fill or line may be empty, and an exact colour field.
// These are the author's document colours, not chrome.
Tk.Dialog {
    id: dialog
    objectName: "diagramColorDialog"

    required property DiagramService diagram
    property string what: "fill"
    property color chosen: "#ffffff"
    readonly property var themeColors: {
        const list = diagram.themes
        for (let i = 0; i < list.length; ++i) if (list[i].id === diagram.themeId) return Array.from(list[i].swatches)
        return []
    }
    readonly property var neutrals: ["#ffffff", "#f1f5f9", "#cbd5e1", "#64748b", "#334155", "#111827",
                                     "#e3eefc", "#fdf1d6", "#ddf3e6", "#fde4e2", "#ece6fb", "#fff6c7"]

    title: what === "fill" ? qsTr("Fill colour") : what === "line" ? qsTr("Line colour")
         : what === "text" ? qsTr("Text colour") : qsTr("Page background")
    primaryText: qsTr("Apply")
    tertiaryText: what === "fill" || what === "line" ? qsTr("None") : ""

    function openFor(kind) {
        what = kind
        const item = diagram.selected
        chosen = kind === "fill" ? (item.fill || "#ffffff") : kind === "line" ? (item.line || "#475569")
               : kind === "text" ? ((item.textStyle || {}).color || "#1f2937") : (diagram.page.background || "#ffffff")
        field.color = chosen
        open()
    }
    function apply(color) {
        if (what === "fill") diagram.setStyle({ fill: color })
        else if (what === "line") diagram.setStyle({ line: color })
        else if (what === "text") diagram.setTextStyle({ color: color })
        else diagram.setPageBackground(color)
    }
    onAccepted: apply(String(chosen))
    onTertiary: { apply("none"); close() }

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.md
        Tk.Overline { title: qsTr("THEME COLOURS") }
        Tk.ColorSwatches {
            objectName: "colorDialogTheme"
            colors: dialog.themeColors
            current: dialog.chosen
            columns: 6
            customButton: false
            onSelected: function(color) { dialog.chosen = color; field.color = color }
        }
        Tk.Overline { title: qsTr("STANDARD") }
        Tk.ColorSwatches {
            objectName: "colorDialogStandard"
            colors: dialog.neutrals
            current: dialog.chosen
            columns: 6
            customButton: false
            onSelected: function(color) { dialog.chosen = color; field.color = color }
        }
        Tk.PropertyRow {
            label: qsTr("Exact")
            Tk.ColorField {
                id: field
                objectName: "colorDialogField"
                tooltip: qsTr("Colour as hex")
                onColorEdited: function(color) { dialog.chosen = color }
            }
        }
    }
}
