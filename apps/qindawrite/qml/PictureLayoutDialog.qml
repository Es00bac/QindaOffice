// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Picture Format ▸ Position and Wrapping (M77, D-086): Word's Layout dialog
// for the selected picture — the wrapping style and side, the distances
// from the text, and the horizontal and vertical position: an alignment
// or an offset (inches) relative to the margin, page, column, paragraph or
// line; Move object with text, Allow overlap. One undo step
// (PictureLayoutController, write_float.h).
Tk.Dialog {
    id: dialog
    required property QtObject pictureLayout
    readonly property var wraps: ["inline", "square", "tight", "through", "topAndBottom", "behind", "inFront"]
    readonly property var sides: ["bothSides", "left", "right", "largest"]
    readonly property var hBases: ["column", "margin", "page", "character", "leftMargin", "rightMargin"]
    readonly property var vBases: ["paragraph", "line", "margin", "page", "topMargin", "bottomMargin"]
    readonly property var hAligns: ["left", "center", "right"]
    readonly property var vAligns: ["top", "center", "bottom"]
    readonly property bool floating: wrap.currentIndex > 0
    objectName: "pictureLayoutDialog"
    title: qsTr("Layout")
    dialogWidth: 560
    primaryText: qsTr("OK")

    Connections {
        target: dialog.pictureLayout
        function onLayoutDialogRequested() {
            const info = dialog.pictureLayout.layoutInfo()
            if (info.wrap === undefined) return
            wrap.currentIndex = Math.max(0, dialog.wraps.indexOf(info.wrap))
            side.currentIndex = Math.max(0, dialog.sides.indexOf(info.wrapText))
            hBase.currentIndex = Math.max(0, dialog.hBases.indexOf(info.hRelative))
            vBase.currentIndex = Math.max(0, dialog.vBases.indexOf(info.vRelative))
            hMode.currentIndex = info.hAlign.length > 0 ? 0 : 1
            vMode.currentIndex = info.vAlign.length > 0 ? 0 : 1
            hAlign.currentIndex = Math.max(0, dialog.hAligns.indexOf(info.hAlign))
            vAlign.currentIndex = Math.max(0, dialog.vAligns.indexOf(info.vAlign))
            hOffset.value = info.hOffset / 96
            vOffset.value = info.vOffset / 96
            distTop.value = info.distTop / 96
            distBottom.value = info.distBottom / 96
            distLeft.value = info.distLeft / 96
            distRight.value = info.distRight / 96
            overlap.checked = info.allowOverlap
            dialog.open()
        }
    }
    onAccepted: dialog.pictureLayout.applyLayout({
        wrap: wraps[wrap.currentIndex], wrapText: sides[side.currentIndex],
        hRelative: hBases[hBase.currentIndex], hAlign: hMode.currentIndex === 0 ? hAligns[hAlign.currentIndex] : "",
        hOffset: hOffset.value * 96, vRelative: vBases[vBase.currentIndex],
        vAlign: vMode.currentIndex === 0 ? vAligns[vAlign.currentIndex] : "", vOffset: vOffset.value * 96,
        distTop: distTop.value * 96, distBottom: distBottom.value * 96, distLeft: distLeft.value * 96,
        distRight: distRight.value * 96, allowOverlap: overlap.checked })

    component Inches: Tk.NumberField {
        implicitWidth: 86
        from: -22
        to: 22
        decimals: 2
        stepSize: 0.1
        suffix: qsTr(" in")
        onValueModified: function(v) { value = v }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Text wrapping") }
        Tk.PropertyRow {
            label: qsTr("Wrapping style")
            Tk.ComboBox {
                id: wrap
                objectName: "layoutWrap"
                model: [qsTr("In line with text"), qsTr("Square"), qsTr("Tight"), qsTr("Through"),
                        qsTr("Top and bottom"), qsTr("Behind text"), qsTr("In front of text")]
            }
        }
        Tk.PropertyRow {
            label: qsTr("Wrap text")
            enabled: wrap.currentIndex >= 1 && wrap.currentIndex <= 3
            Tk.ComboBox { id: side; objectName: "layoutSide"; model: [qsTr("Both sides"), qsTr("Left only"), qsTr("Right only"), qsTr("Largest only")] }
        }
        Tk.Overline { text: qsTr("Distance from text") }
        // Word's two columns: top and bottom, left and right.
        Tk.Grid {
            enabled: dialog.floating
            columns: "auto 1fr auto 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center
            Tk.Label { text: qsTr("Top") }
            Inches { id: distTop; objectName: "layoutDistTop"; from: 0 }
            Tk.Label { text: qsTr("Left") }
            Inches { id: distLeft; objectName: "layoutDistLeft"; from: 0 }
            Tk.Label { text: qsTr("Bottom") }
            Inches { id: distBottom; objectName: "layoutDistBottom"; from: 0 }
            Tk.Label { text: qsTr("Right") }
            Inches { id: distRight; objectName: "layoutDistRight"; from: 0 }
        }
        Tk.Overline { text: qsTr("Horizontal position") }
        Tk.Flex {
            enabled: dialog.floating
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Segmented { id: hMode; objectName: "layoutHMode"; small: true; model: [qsTr("Alignment"), qsTr("Absolute")] }
            Tk.ComboBox { id: hAlign; objectName: "layoutHAlign"; small: true; implicitWidth: 90; visible: hMode.currentIndex === 0; model: [qsTr("Left"), qsTr("Centered"), qsTr("Right")] }
            Inches { id: hOffset; objectName: "layoutHOffset"; visible: hMode.currentIndex === 1 }
            Tk.Label { text: qsTr("relative to") }
            Tk.ComboBox { id: hBase; objectName: "layoutHBase"; small: true; implicitWidth: 120; model: [qsTr("Column"), qsTr("Margin"), qsTr("Page"), qsTr("Character"), qsTr("Left margin"), qsTr("Right margin")] }
        }
        Tk.Overline { text: qsTr("Vertical position") }
        Tk.Flex {
            enabled: dialog.floating
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Segmented { id: vMode; objectName: "layoutVMode"; small: true; model: [qsTr("Alignment"), qsTr("Absolute")] }
            Tk.ComboBox { id: vAlign; objectName: "layoutVAlign"; small: true; implicitWidth: 90; visible: vMode.currentIndex === 0; model: [qsTr("Top"), qsTr("Centered"), qsTr("Bottom")] }
            Inches { id: vOffset; objectName: "layoutVOffset"; visible: vMode.currentIndex === 1 }
            Tk.Label { text: qsTr("below") }
            Tk.ComboBox { id: vBase; objectName: "layoutVBase"; small: true; implicitWidth: 120; model: [qsTr("Paragraph"), qsTr("Line"), qsTr("Margin"), qsTr("Page"), qsTr("Top margin"), qsTr("Bottom margin")] }
        }
        Tk.CheckBox {
            objectName: "layoutMoveWithText"
            enabled: dialog.floating
            text: qsTr("Move object with text")
            checked: vBase.currentIndex <= 1
            onClicked: vBase.currentIndex = checked ? 0 : 3
        }
        Tk.CheckBox { id: overlap; objectName: "layoutOverlap"; enabled: dialog.floating; text: qsTr("Allow overlap") }
    }
}
