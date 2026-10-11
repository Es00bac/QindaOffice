// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Slide Show ▸ Set Up Slide Show… (M91, D-099), as PowerPoint's Set Up
// Show: the show type (presented by a speaker, browsed by an individual in
// a window, browsed at a kiosk — which loops and no click advances), its
// options (loop until Esc, without narration, without animation, the pen
// and laser colours), the slides (all, from–to, a custom show) and how
// they advance (by hand, or by their timings). One undo step
// (ShowSlideShow::applySettings); written as presProps.xml's p:showPr.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowSlideShow slideShow: showHost.slideShow
    property var settings: ({})
    readonly property int slideCount: dialog.slideShow.slides.length
    // The pen and laser colours PowerPoint offers first (show colours,
    // not the window's: what the presenter draws on the slides).
    readonly property var inks: ["#ff0000", "#ffc000", "#ffff00", "#00b050", "#00b0f0", "#0070c0", "#7030a0", "#000000", "#ffffff"]

    function set(key, value) {
        const next = Object.assign({}, dialog.settings)
        next[key] = value
        dialog.settings = next
    }

    objectName: "setUpShowDialog"
    title: qsTr("Set Up Show")
    primaryText: qsTr("OK")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    onAboutToShow: {
        dialog.settings = Object.assign({}, dialog.slideShow.settings)
        fromField.value = dialog.settings.from
        toField.value = dialog.settings.to
        customCombo.currentIndex = Math.max(0, dialog.settings.customShow)
    }
    onAccepted: dialog.slideShow.applySettings(dialog.settings)

    Tk.Flex {
        direction: dialog.width < dialog.dialogWidth ? Tk.Flex.Column : Tk.Flex.Row
            wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.lg
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Overline { text: qsTr("Show type") }
            Tk.RadioButton {
                objectName: "setUpSpeaker"
                checkable: false
                text: qsTr("Presented by a speaker (full screen)")
                checked: dialog.settings.type === "speaker"
                onClicked: dialog.set("type", "speaker")
            }
            Tk.RadioButton {
                objectName: "setUpWindow"
                checkable: false
                text: qsTr("Browsed by an individual (window)")
                checked: dialog.settings.type === "window"
                onClicked: dialog.set("type", "window")
            }
            Tk.RadioButton {
                objectName: "setUpKiosk"
                checkable: false
                text: qsTr("Browsed at a kiosk (full screen)")
                checked: dialog.settings.type === "kiosk"
                onClicked: dialog.set("type", "kiosk")
            }
            Tk.Overline { text: qsTr("Show options") }
            Tk.CheckBox {
                objectName: "setUpLoop"
                checkable: false
                text: qsTr("Loop continuously until ‘Esc’")
                enabled: dialog.settings.type !== "kiosk"
                checked: dialog.settings.loop === true || dialog.settings.type === "kiosk"
                onClicked: dialog.set("loop", !checked)
            }
            Tk.CheckBox {
                objectName: "setUpNoNarration"
                checkable: false
                text: qsTr("Show without narration")
                checked: dialog.settings.narration === false
                onClicked: dialog.set("narration", checked)
            }
            Tk.CheckBox {
                objectName: "setUpNoAnimation"
                checkable: false
                text: qsTr("Show without animation")
                checked: dialog.settings.animation === false
                onClicked: dialog.set("animation", checked)
            }
            Tk.PropertyRow {
                label: qsTr("Pen color")
                Tk.ColorButton {
                    objectName: "setUpPen"
                    color: dialog.settings.penColor !== undefined ? dialog.settings.penColor : "#ff0000"
                    colors: dialog.inks
                    tooltip: qsTr("Pen color")
                    onColorSelected: function(c) { dialog.set("penColor", c) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Laser pointer color")
                Tk.ColorButton {
                    objectName: "setUpLaser"
                    color: dialog.settings.laserColor !== undefined ? dialog.settings.laserColor : "#ff0000"
                    colors: dialog.inks
                    tooltip: qsTr("Laser pointer color")
                    onColorSelected: function(c) { dialog.set("laserColor", c) }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Overline { text: qsTr("Show slides") }
            Tk.RadioButton {
                objectName: "setUpAll"
                checkable: false
                text: qsTr("All")
                checked: dialog.settings.range === "all"
                onClicked: dialog.set("range", "all")
            }
            Tk.Flex {
                direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                align: Tk.Flex.Center
                Tk.RadioButton {
                    objectName: "setUpRange"
                    checkable: false
                    text: qsTr("From:")
                    checked: dialog.settings.range === "slides"
                    onClicked: dialog.set("range", "slides")
                }
                Tk.NumberField {
                    id: fromField
                    objectName: "setUpFrom"
                    enabled: dialog.settings.range === "slides"
                    implicitWidth: Tk.Theme.size.control * 3
                    from: 1
                    to: Math.max(1, dialog.slideCount)
                    decimals: 0
                    scrub: false
                    onValueModified: function(next) { value = next; dialog.set("from", Math.round(next)) }
                }
                Tk.Label { text: qsTr("To:") }
                Tk.NumberField {
                    id: toField
                    objectName: "setUpTo"
                    enabled: dialog.settings.range === "slides"
                    implicitWidth: Tk.Theme.size.control * 3
                    from: 1
                    to: Math.max(1, dialog.slideCount)
                    decimals: 0
                    scrub: false
                    onValueModified: function(next) { value = next; dialog.set("to", Math.round(next)) }
                }
            }
            Tk.RadioButton {
                objectName: "setUpCustom"
                checkable: false
                text: qsTr("Custom show:")
                enabled: dialog.slideShow.customShows.length > 0
                checked: dialog.settings.range === "custom"
                onClicked: {
                    dialog.set("range", "custom")
                    if (!(dialog.settings.customShow >= 0)) dialog.set("customShow", 0)
                }
            }
            Tk.ComboBox {
                id: customCombo
                objectName: "setUpCustomShow"
                enabled: dialog.settings.range === "custom"
                width: Tk.Theme.size.dialogWidth * 0.39
                model: dialog.slideShow.customShows.map(function(s) { return s.name })
                onActivated: function(index) { dialog.set("customShow", index) }
            }
            Tk.Overline { text: qsTr("Advance slides") }
            Tk.RadioButton {
                objectName: "setUpManually"
                checkable: false
                text: qsTr("Manually")
                checked: dialog.settings.useTimings === false
                onClicked: dialog.set("useTimings", false)
            }
            Tk.RadioButton {
                objectName: "setUpTimings"
                checkable: false
                text: qsTr("Using timings, if present")
                checked: dialog.settings.useTimings === true
                onClicked: dialog.set("useTimings", true)
            }
        }
    }
}
