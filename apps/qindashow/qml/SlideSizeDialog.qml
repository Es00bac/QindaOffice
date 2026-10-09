// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Design ▸ Slide Size ▸ Custom Slide Size… (M75, D-084), as PowerPoint's
// dialog: a preset (the sizes PowerPoint lists), or a width and height of
// one's own, and the orientation. Lengths show in centimetres where the
// locale is metric, else in inches; `sizeChosen` answers in points, and
// ShowDesignTab.requestSlideSize then asks Maximize or Ensure Fit when the
// content would have to shrink.
Tk.Dialog {
    id: dialog

    readonly property bool metric: Qt.locale().measurementSystem === Locale.MetricSystem
    readonly property real unit: metric ? 72 / 2.54 : 72      // points per shown unit
    readonly property string suffix: metric ? qsTr(" cm") : qsTr(" in")
    // PowerPoint's presets, in points; the last row is the custom size.
    readonly property var presets: [
        { name: qsTr("Widescreen"), width: 960, height: 540 },
        { name: qsTr("Standard (4:3)"), width: 720, height: 540 },
        { name: qsTr("On-screen Show (16:9)"), width: 720, height: 405 },
        { name: qsTr("On-screen Show (16:10)"), width: 720, height: 450 },
        { name: qsTr("Letter Paper (8.5×11 in)"), width: 720, height: 540 },
        { name: qsTr("A4 Paper (210×297 mm)"), width: 780, height: 540 },
        { name: qsTr("35mm Slides"), width: 810, height: 540 },
        { name: qsTr("Banner"), width: 576, height: 72 },
        { name: qsTr("Custom"), width: 0, height: 0 }]

    signal sizeChosen(real width, real height)

    // Opens on the deck's size (points). PowerPoint's limits are 1 to 56
    // inches; the fields' bounds are set here, not bound: a bound `from`
    // evaluated late re-runs NumberField's clampedValue (a binding loop).
    function load(width, height) {
        for (const field of [widthField, heightField]) {
            field.from = metric ? 2.54 : 1
            field.to = metric ? 142.24 : 56
        }
        widthField.value = width / unit
        heightField.value = height / unit
        presetBox.currentIndex = presetFor(width, height)
        orientation.currentIndex = width >= height ? 1 : 0
    }
    function presetFor(width, height) {
        for (let i = 0; i < presets.length - 1; ++i) {
            if (Math.abs(presets[i].width - width) < 0.5 && Math.abs(presets[i].height - height) < 0.5) return i
        }
        return presets.length - 1
    }

    objectName: "slideSizeDialog"
    title: qsTr("Slide Size")
    primaryText: qsTr("OK")
    onAccepted: dialog.sizeChosen(widthField.value * unit, heightField.value * unit)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Slides sized for")
            Tk.ComboBox {
                id: presetBox
                objectName: "slideSizePreset"
                width: 240
                model: dialog.presets.map(function(p) { return p.name })
                onActivated: function(index) {
                    const p = dialog.presets[index]
                    if (p.width > 0) {
                        widthField.value = p.width / dialog.unit
                        heightField.value = p.height / dialog.unit
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Width")
            Tk.NumberField {
                id: widthField
                objectName: "slideSizeWidth"
                value: 13.33
                from: 1
                to: 142.24
                decimals: 2
                stepSize: 0.1
                scrub: false
                suffix: dialog.suffix
                onValueModified: presetBox.currentIndex = dialog.presetFor(value * dialog.unit, heightField.value * dialog.unit)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Height")
            Tk.NumberField {
                id: heightField
                objectName: "slideSizeHeight"
                value: 7.5
                from: 1
                to: 142.24
                decimals: 2
                stepSize: 0.1
                scrub: false
                suffix: dialog.suffix
                onValueModified: presetBox.currentIndex = dialog.presetFor(widthField.value * dialog.unit, value * dialog.unit)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented {
                id: orientation
                objectName: "slideSizeOrientation"
                small: true
                model: [qsTr("Portrait"), qsTr("Landscape")]
                onActivated: function(index) {
                    const portrait = index === 0
                    if (portrait === (widthField.value > heightField.value)) {
                        const w = widthField.value
                        widthField.value = heightField.value
                        heightField.value = w
                        presetBox.currentIndex = dialog.presetFor(widthField.value * dialog.unit, heightField.value * dialog.unit)
                    }
                }
            }
        }
    }
}
