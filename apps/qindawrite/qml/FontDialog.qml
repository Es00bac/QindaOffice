// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format ▸ Font (Ctrl+D, M70, D-079): Word's Font dialog. Font — family,
// style, size, colour, underline style and colour, and the effects
// (strikethrough, double strikethrough, superscript, subscript, small caps,
// all caps, hidden); Advanced — character spacing expanded or condensed by
// points, scale, position raised or lowered, kerning from a size up. Only
// what the user touches is applied (`changed`); the host (write_font.h)
// applies it run by run, so a mixed selection keeps the rest.
Tk.Dialog {
    id: dialog
    signal fontApplied(var answer)
    signal customColorRequested(string what)
    property var families: []
    property var sizes: []
    property var changed: ({})
    property bool loading: false
    property string color: ""
    property string underlineColor: ""
    readonly property var underlines: ["none", "words", "single", "double", "thick", "dotted", "dash", "dotDash", "dotDotDash", "wave"]
    objectName: "fontDialog"
    title: qsTr("Font")
    dialogWidth: 540
    primaryText: qsTr("OK")

    function mark(key) {
        if (loading) return
        const next = Object.assign({}, changed)
        next[key] = true
        changed = next
    }
    function load(info) {
        loading = true
        changed = ({})
        pages.currentIndex = 0
        family.currentIndex = Math.max(0, families.indexOf(info.family))
        style.currentIndex = (info.bold ? 2 : 0) + (info.italic ? 1 : 0)
        size.editText = String(Math.round(info.size * 10) / 10)
        color = info.color
        underline.currentIndex = Math.max(0, underlines.indexOf(info.underline))
        underlineColor = info.underlineColor
        strike.checked = info.strike
        doubleStrike.checked = info.doubleStrike
        superscript.checked = info.superscript
        subscript.checked = info.subscript
        smallCaps.checked = info.smallCaps
        allCaps.checked = info.allCaps
        hidden.checked = info.hidden
        scale.value = info.scale
        spacingKind.currentIndex = info.spacing > 0.005 ? 1 : info.spacing < -0.005 ? 2 : 0
        spacingBy.value = Math.abs(info.spacing)
        positionKind.currentIndex = info.position > 0.005 ? 1 : info.position < -0.005 ? 2 : 0
        positionBy.value = Math.abs(info.position)
        kerning.checked = info.kerning
        kernAt.value = info.kernAt
        loading = false
    }
    onAccepted: {
        if (Object.keys(changed).length === 0) return
        const signed = function(kind, by) { return kind === 1 ? by : kind === 2 ? -by : 0 }
        dialog.fontApplied({
            changed: Object.keys(changed),
            family: families[family.currentIndex], size: parseFloat(size.editText),
            bold: style.currentIndex >= 2, italic: style.currentIndex % 2 === 1,
            color: color, underline: underlines[underline.currentIndex], underlineColor: underlineColor,
            strike: strike.checked, doubleStrike: doubleStrike.checked,
            superscript: superscript.checked, subscript: subscript.checked,
            smallCaps: smallCaps.checked, allCaps: allCaps.checked, hidden: hidden.checked,
            scale: scale.value, spacing: signed(spacingKind.currentIndex, spacingBy.value),
            position: signed(positionKind.currentIndex, positionBy.value),
            kerning: kerning.checked, kernAt: kernAt.value
        })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            id: pages
            objectName: "fontPages"
            model: [qsTr("Font"), qsTr("Advanced")]
        }
        Tk.Flex {
            visible: pages.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyRow {
                label: qsTr("Font")
                Tk.ComboBox { id: family; objectName: "fontFamily"; model: dialog.families; onActivated: dialog.mark("family") }
            }
            Tk.PropertyRow {
                label: qsTr("Style / size")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ComboBox {
                        id: style
                        objectName: "fontStyle"
                        model: [qsTr("Regular"), qsTr("Italic"), qsTr("Bold"), qsTr("Bold Italic")]
                        onActivated: { dialog.mark("bold"); dialog.mark("italic") }
                    }
                    Tk.ComboBox {
                        id: size
                        objectName: "fontSize"
                        editable: true
                        implicitWidth: 80
                        model: dialog.sizes
                        onActivated: function(index) { editText = dialog.sizes[index]; dialog.mark("size") }
                        onAccepted: dialog.mark("size")
                    }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Font colour")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ColorButton {
                        objectName: "fontColor"
                        ghost: false
                        tooltip: qsTr("Font colour")
                        color: dialog.color.length > 0 ? dialog.color : "#000000"
                        colors: ["#000000", "#7f7f7f", "#1f4e79", "#2f5496", "#4472c4", "#c00000", "#ed7d31", "#70ad47", "#7030a0"]
                        onColorSelected: function(c) { dialog.color = String(c); dialog.mark("color") }
                        onCustomRequested: dialog.customColorRequested("font")
                    }
                    Tk.Button { objectName: "fontColorAuto"; text: qsTr("Automatic"); onClicked: { dialog.color = ""; dialog.mark("color") } }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Underline / colour")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ComboBox {
                        id: underline
                        objectName: "fontUnderline"
                        model: [qsTr("(none)"), qsTr("Words only"), qsTr("Single"), qsTr("Double"), qsTr("Thick"), qsTr("Dotted"),
                                qsTr("Dash"), qsTr("Dot dash"), qsTr("Dot dot dash"), qsTr("Wave")]
                        onActivated: dialog.mark("underline")
                    }
                    Tk.ColorButton {
                        objectName: "fontUnderlineColor"
                        ghost: false
                        enabled: underline.currentIndex > 0
                        tooltip: qsTr("Underline colour")
                        color: dialog.underlineColor.length > 0 ? dialog.underlineColor : "#000000"
                        colors: ["#000000", "#7f7f7f", "#2f5496", "#c00000", "#ed7d31", "#70ad47"]
                        onColorSelected: function(c) { dialog.underlineColor = String(c); dialog.mark("underlineColor") }
                        onCustomRequested: dialog.customColorRequested("underline")
                    }
                }
            }
            Tk.PropertyGroup {
                title: qsTr("Effects")
                collapsible: false
                Tk.Flex {
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.sm
                    Tk.CheckBox { id: strike; objectName: "fontStrike"; text: qsTr("Strikethrough")
                                  onToggled: { if (checked) doubleStrike.checked = false; dialog.mark("strike"); dialog.mark("doubleStrike") } }
                    Tk.CheckBox { id: doubleStrike; objectName: "fontDoubleStrike"; text: qsTr("Double strikethrough")
                                  onToggled: { if (checked) strike.checked = false; dialog.mark("strike"); dialog.mark("doubleStrike") } }
                    Tk.CheckBox { id: superscript; objectName: "fontSuperscript"; text: qsTr("Superscript")
                                  onToggled: { if (checked) subscript.checked = false; dialog.mark("superscript"); dialog.mark("subscript") } }
                    Tk.CheckBox { id: subscript; objectName: "fontSubscript"; text: qsTr("Subscript")
                                  onToggled: { if (checked) superscript.checked = false; dialog.mark("superscript"); dialog.mark("subscript") } }
                    Tk.CheckBox { id: smallCaps; objectName: "fontSmallCaps"; text: qsTr("Small caps")
                                  onToggled: { if (checked) allCaps.checked = false; dialog.mark("smallCaps"); dialog.mark("allCaps") } }
                    Tk.CheckBox { id: allCaps; objectName: "fontAllCaps"; text: qsTr("All caps")
                                  onToggled: { if (checked) smallCaps.checked = false; dialog.mark("smallCaps"); dialog.mark("allCaps") } }
                    Tk.CheckBox { id: hidden; objectName: "fontHidden"; text: qsTr("Hidden"); onToggled: dialog.mark("hidden") }
                }
            }
        }
        Tk.Flex {
            visible: pages.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyRow {
                label: qsTr("Scale")
                Tk.NumberField { id: scale; objectName: "fontScale"; from: 1; to: 600; stepSize: 10; decimals: 0; suffix: qsTr(" %")
                                 onValueModified: function(v) { value = v; dialog.mark("scale") } }
            }
            Tk.PropertyRow {
                label: qsTr("Spacing / by")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ComboBox { id: spacingKind; objectName: "fontSpacingKind"; model: [qsTr("Normal"), qsTr("Expanded"), qsTr("Condensed")]
                                  onActivated: dialog.mark("spacing") }
                    Tk.NumberField { id: spacingBy; objectName: "fontSpacingBy"; enabled: spacingKind.currentIndex > 0
                                     from: 0; to: 1584; stepSize: 0.1; decimals: 1; suffix: qsTr(" pt")
                                     onValueModified: function(v) { value = v; dialog.mark("spacing") } }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Position / by")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ComboBox { id: positionKind; objectName: "fontPositionKind"; model: [qsTr("Normal"), qsTr("Raised"), qsTr("Lowered")]
                                  onActivated: dialog.mark("position") }
                    Tk.NumberField { id: positionBy; objectName: "fontPositionBy"; enabled: positionKind.currentIndex > 0
                                     from: 0; to: 1584; stepSize: 0.5; decimals: 1; suffix: qsTr(" pt")
                                     onValueModified: function(v) { value = v; dialog.mark("position") } }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Kerning for fonts")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.CheckBox { id: kerning; objectName: "fontKerning"; text: qsTr("from"); onToggled: dialog.mark("kerning") }
                    Tk.NumberField { id: kernAt; objectName: "fontKernAt"; enabled: kerning.checked; from: 1; to: 1638; stepSize: 1
                                     decimals: 1; suffix: qsTr(" pt and above"); onValueModified: function(v) { value = v; dialog.mark("kernAt") } }
                }
            }
        }
        // The preview: the selection's look on paper (white page, D-004).
        Rectangle {
            objectName: "fontPreview"
            implicitWidth: 300
            implicitHeight: 56
            color: "white"
            Tk.Label {
                anchors.centerIn: parent
                elide: Text.ElideNone
                text: allCaps.checked ? family.currentText.toUpperCase() : family.currentText
                font.family: family.currentText
                font.pixelSize: Math.min(40, Math.max(8, parseFloat(size.editText) * 4 / 3 || 14))
                font.bold: style.currentIndex >= 2
                font.italic: style.currentIndex % 2 === 1
                font.underline: underline.currentIndex > 0
                font.strikeout: strike.checked || doubleStrike.checked
                font.capitalization: smallCaps.checked ? Font.SmallCaps : Font.MixedCase
                font.letterSpacing: (spacingKind.currentIndex === 1 ? 1 : spacingKind.currentIndex === 2 ? -1 : 0) * spacingBy.value * 4 / 3
                color: dialog.color.length > 0 ? dialog.color : "black"
            }
        }
    }
}
