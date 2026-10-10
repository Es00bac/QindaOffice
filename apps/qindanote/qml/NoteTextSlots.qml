// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// The Text tab's widgets (M84, D-093; OneNote's Basic Text): font, size,
// font colour and highlight. They show the open note's selection
// (NoteHost.textFormat) and hand the keyboard back to the editor after a
// choice. The colours are paper colours (D-004).
QtObject {
    id: slots

    required property NoteHost note
    signal customColorRequested(string target)

    readonly property Tk.FontCatalog catalog: Tk.FontCatalog {}
    readonly property var sizes: ["8", "9", "10", "11", "12", "14", "16", "18", "20", "24", "28", "36", "48", "72"]

    property Component font: Component {
        Tk.ComboBox {
            id: family
            objectName: "noteFontFamily"
            small: true
            editable: true
            enabled: slots.note.textFormat.editable === true
            implicitWidth: 150
            model: slots.catalog.families
            textRole: "family"
            tooltip: qsTr("Font")
            function sync() { if (!family.activeFocus) family.editText = slots.note.textFormat.family || "" }
            Component.onCompleted: sync()
            onActivated: function(index) { slots.note.applyFontFamily(family.textAt(index)) }
            onAccepted: slots.note.applyFontFamily(family.editText)
            Connections {
                target: slots.note
                function onTextFormatChanged() { family.sync() }
            }
        }
    }
    property Component size: Component {
        Tk.ComboBox {
            id: size
            objectName: "noteFontSize"
            small: true
            editable: true
            enabled: slots.note.textFormat.editable === true
            implicitWidth: 64
            model: slots.sizes
            tooltip: qsTr("Font size")
            function sync() {
                if (!size.activeFocus) size.editText = slots.note.textFormat.size > 0
                                       ? String(Math.round(slots.note.textFormat.size * 10) / 10) : ""
            }
            Component.onCompleted: sync()
            onActivated: function(index) { slots.note.applyFontSize(Number(slots.sizes[index])) }
            onAccepted: slots.note.applyFontSize(Number(size.editText))
            Connections {
                target: slots.note
                function onTextFormatChanged() { size.sync() }
            }
        }
    }
    property Component textColor: Component {
        Tk.ColorButton {
            objectName: "noteTextColor"
            small: true
            enabled: slots.note.textFormat.editable === true
            tooltip: qsTr("Font colour")
            color: slots.note.textFormat.color !== undefined ? slots.note.textFormat.color : "#202028"
            colors: ["#202028", "#7f7f7f", "#1a5fb4", "#c01c28", "#26a269", "#e66100", "#813d9c", "#8d5a2b"]
            onColorSelected: function(c) { slots.note.applyTextColor(c) }
            onCustomRequested: slots.customColorRequested("text")
        }
    }
    property Component highlight: Component {
        Tk.ColorButton {
            objectName: "noteHighlightColor"
            small: true
            enabled: slots.note.textFormat.editable === true
            tooltip: qsTr("Text highlight colour")
            color: slots.note.textFormat.highlight !== undefined && slots.note.textFormat.highlight.a > 0
                   ? slots.note.textFormat.highlight : "#ffe14d"
            // Office's marker colours; white takes the highlight off.
            colors: ["#ffe14d", "#8ff0a4", "#99c1f1", "#f5c2e7", "#ffbe6f", "#dc8add", "#ffffff"]
            onColorSelected: function(c) { slots.note.applyHighlight(Qt.colorEqual(c, "#ffffff") ? Qt.rgba(0, 0, 0, 0) : c) }
            onCustomRequested: slots.customColorRequested("highlight")
        }
    }
}
