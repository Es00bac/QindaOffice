// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Home ▸ Styles ▸ Styles Pane (M73, D-082; Alt+Ctrl+Shift+S): the
// document's paragraph, character, linked and table styles and Word's
// built-ins it lacks, in Word's order. A click applies a style (a built-in
// joins the document as in Word); the caret's style is marked; Show Preview
// draws each name in its style's font; "Only styles in use" hides the
// rest. New Style, Modify, Update to Match Selection and Delete act on the
// marked style (StylesController, write_styles.h).
Tk.Flex {
    id: panel

    required property var styles
    property string selectedId: styles.currentStyle
    readonly property var rows: styles.styles.filter(function(row) { return !onlyInUse.checked || row.inUse })
    readonly property var typeLabels: ({ paragraph: qsTr("Paragraph"), character: qsTr("Character"),
                                         linked: qsTr("Linked"), table: qsTr("Table") })

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Connections {
        target: panel.styles
        function onCurrentChanged() { panel.selectedId = panel.styles.currentStyle }
    }

    Tk.Flex {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.sm
        Tk.CheckBox { id: preview; objectName: "stylesPreview"; small: true; text: qsTr("Show Preview") }
        Tk.CheckBox { id: onlyInUse; objectName: "stylesInUse"; small: true; text: qsTr("Only styles in use") }
    }
    Tk.Scroll {
        id: scroll
        objectName: "stylesScroll"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 80
        overflowX: Tk.Scroll.Hidden
        Tk.Flex {
            direction: Tk.Flex.Column
            width: scroll.width - Tk.Theme.space.sm
            Repeater {
                model: panel.rows
                Tk.ListRow {
                    id: row
                    required property var modelData
                    objectName: "style_" + modelData.id
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    text: preview.checked ? "" : modelData.name
                    secondaryText: panel.typeLabels[modelData.type] + (modelData.inUse ? qsTr(" · in use") : "")
                                   + (modelData.inDocument ? "" : qsTr(" · Word built-in"))
                    selected: panel.selectedId === modelData.id
                    tooltip: qsTr("Apply %1").arg(modelData.name)
                    onClicked: {
                        panel.selectedId = modelData.id
                        panel.styles.apply(modelData.id)
                    }
                    // Show Preview: the name in the style's own font and colour
                    // (the document's content, not chrome).
                    leading: Tk.Label {
                        objectName: "stylePreview"
                        visible: preview.checked
                        text: row.modelData.name
                        color: row.modelData.color.length > 0 ? row.modelData.color : Tk.Theme.color.text
                        font.family: row.modelData.family
                        font.pixelSize: Math.min(row.modelData.size * 96 / 72, Tk.Theme.size.rowLg * 0.6)
                        font.bold: row.modelData.bold
                        font.italic: row.modelData.italic
                    }
                }
            }
        }
    }
    Tk.Flex {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.xs
        Tk.Button { objectName: "stylesNew"; small: true; iconName: "plus"; text: qsTr("New Style…"); onClicked: panel.styles.newStyle() }
        Tk.Button {
            objectName: "stylesModify"; small: true; iconName: "square-pen"; text: qsTr("Modify…")
            onClicked: panel.styles.modify(panel.selectedId)
        }
        Tk.Button {
            objectName: "stylesUpdate"; small: true; iconName: "refresh-cw"; text: qsTr("Update to Match")
            onClicked: panel.styles.updateToMatch(panel.selectedId)
        }
        Tk.Button {
            objectName: "stylesDelete"; small: true; iconName: "trash-2"; text: qsTr("Delete")
            enabled: panel.styles.canRemove(panel.selectedId)
            onClicked: panel.styles.remove(panel.selectedId)
        }
    }
}
