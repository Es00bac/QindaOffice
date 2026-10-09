// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Show

// Design ▸ Format Background (M75, D-084), as PowerPoint's pane: the fill
// of the current slide's background (in the Slide Master view, of the
// master or layout on the canvas) — Solid, Gradient (linear or radial, its
// stops and angle), Picture or texture (a file or one of QindaShow's
// tiles, tiled or stretched), Pattern — with Transparency, Hide Background
// Graphics, Apply to All and Reset Background. The pane only shows
// ShowBackground's state and calls its setters: each change is one undo
// step, and undo shows here as the state comes back.
Tk.Scroll {
    id: pane
    required property ShowHost showHost
    readonly property ShowBackground bg: showHost.background
    readonly property var kinds: ["solid", "gradient", "picture", "pattern"]
    // The gradient stop the colour field edits, and a drag in progress.
    property var selectedStop: 0
    property var preview: null
    // fill | stop | foreground | background: what the colour dialog sets.
    property string colorTarget: "fill"

    readonly property var patternNames: ({
        pct10: qsTr("10%"), pct25: qsTr("25%"), pct50: qsTr("50%"), pct75: qsTr("75%"),
        ltHorz: qsTr("Light horizontal"), ltVert: qsTr("Light vertical"),
        dkHorz: qsTr("Dark horizontal"), dkVert: qsTr("Dark vertical"),
        ltDnDiag: qsTr("Light downward diagonal"), ltUpDiag: qsTr("Light upward diagonal"),
        smGrid: qsTr("Small grid"), lgGrid: qsTr("Large grid"),
        smCheck: qsTr("Small checkerboard"), diagCross: qsTr("Diagonal cross") })

    objectName: "formatBackgroundPane"

    function stopsWith(id, change) {
        return bg.stops.map(function(s) { return s.id === id ? Object.assign({}, s, change) : s })
    }
    function setStops(stops) { bg.setGradient(stops, bg.angle, bg.radial) }
    function stopColor() {
        for (const s of bg.stops) if (s.id === pane.selectedStop) return s.color
        return bg.stops.length > 0 ? bg.stops[0].color : bg.color
    }
    function pick(target, color) {
        pane.colorTarget = target
        colorDialog.selectedColor = color
        colorDialog.open()
    }
    function applyColor(target, color) {
        if (target === "stop") pane.setStops(pane.stopsWith(pane.selectedStop, { color: color }))
        else if (target === "foreground") bg.setPattern(bg.pattern, color, bg.patternBackground)
        else if (target === "background") bg.setPattern(bg.pattern, bg.patternForeground, color)
        else bg.setColor(color)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        width: pane.width

        Tk.Caption {
            objectName: "backgroundTarget"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            wrapMode: Text.Wrap
            text: pane.bg.target === "master" ? qsTr("The slide master's background")
                : pane.bg.target === "layout" ? qsTr("This layout's background")
                : pane.bg.own ? qsTr("This slide's own background") : qsTr("This slide follows its layout's background")
        }
        Tk.Segmented {
            objectName: "backgroundKind"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            stretch: true
            small: true
            model: [{ text: qsTr("Solid"), tooltip: qsTr("Solid fill") },
                    { text: qsTr("Gradient"), tooltip: qsTr("Gradient fill") },
                    { text: qsTr("Picture"), tooltip: qsTr("Picture or texture fill") },
                    { text: qsTr("Pattern"), tooltip: qsTr("Pattern fill") }]
            currentIndex: Math.max(0, pane.kinds.indexOf(pane.bg.kind))
            onActivated: function(index) { pane.bg.setKind(pane.kinds[index]) }
        }
        Tk.CheckBox {
            objectName: "backgroundHideGraphics"
            small: true
            text: qsTr("Hide background graphics")
            enabled: pane.bg.target !== "master"
            checked: pane.bg.hideGraphics
            onToggled: pane.bg.setHideGraphics(checked)
        }

        // ---- solid ----
        Tk.PropertyRow {
            visible: pane.bg.kind === "solid"
            label: qsTr("Colour")
            dense: true
            Tk.ColorField {
                objectName: "backgroundColor"
                small: true
                color: pane.bg.color
                onColorEdited: function(color) { pane.bg.setColor(color) }
                onPickRequested: pane.pick("fill", pane.bg.color)
            }
        }

        // ---- gradient ----
        Tk.PropertyRow {
            visible: pane.bg.kind === "gradient"
            label: qsTr("Type")
            dense: true
            Tk.Segmented {
                objectName: "backgroundGradientType"
                small: true
                model: [qsTr("Linear"), qsTr("Radial")]
                currentIndex: pane.bg.radial ? 1 : 0
                onActivated: function(index) { pane.bg.setGradient(pane.bg.stops, pane.bg.angle, index === 1) }
            }
        }
        Tk.PropertyRow {
            visible: pane.bg.kind === "gradient" && !pane.bg.radial
            label: qsTr("Angle")
            dense: true
            Tk.NumberField {
                objectName: "backgroundGradientAngle"
                small: true
                from: 0
                to: 359.9
                decimals: 1
                scrub: false
                suffix: "°"
                value: pane.bg.angle
                onValueModified: function(value) { pane.bg.setGradient(pane.bg.stops, value, false) }
            }
        }
        Tk.GradientEditor {
            objectName: "backgroundGradientStops"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: pane.bg.kind === "gradient"
            stops: pane.preview !== null ? pane.preview : pane.bg.stops
            selectedStopId: pane.selectedStop
            tooltip: qsTr("Gradient stops: click adds, drag moves, Delete removes")
            onSelectionRequested: function(id) { pane.selectedStop = id }
            onAddStopRequested: function(position) {
                const stops = pane.bg.stops.slice()
                stops.push({ id: stops.length, position: position, color: pane.stopColor() })
                pane.setStops(stops)
            }
            onMovePreviewRequested: function(id, position) { pane.preview = pane.stopsWith(id, { position: position }) }
            onMoveFinished: function(id, position) {
                pane.preview = null
                pane.setStops(pane.stopsWith(id, { position: position }))
            }
            onMoveCanceled: pane.preview = null
            onDeleteStopRequested: function(id) {
                if (pane.bg.stops.length > 2) pane.setStops(pane.bg.stops.filter(function(s) { return s.id !== id }))
            }
        }
        Tk.PropertyRow {
            visible: pane.bg.kind === "gradient"
            label: qsTr("Stop colour")
            dense: true
            Tk.ColorField {
                objectName: "backgroundStopColor"
                small: true
                color: pane.stopColor()
                onColorEdited: function(color) { pane.applyColor("stop", color) }
                onPickRequested: pane.pick("stop", pane.stopColor())
            }
        }

        // ---- picture or texture ----
        Tk.PropertyRow {
            visible: pane.bg.kind === "picture"
            label: qsTr("Picture")
            dense: true
            Tk.Button {
                objectName: "backgroundInsertPicture"
                small: true
                iconName: "image-plus"
                text: qsTr("Insert…")
                onClicked: pictureDialog.open()
            }
        }
        Tk.PropertyRow {
            visible: pane.bg.kind === "picture"
            label: qsTr("Texture")
            dense: true
            Tk.ComboBox {
                objectName: "backgroundTexture"
                small: true
                model: pane.bg.textures
                placeholderText: qsTr("Choose a texture")
                currentIndex: -1
                onActivated: function(index) { pane.bg.setTexture(index) }
            }
        }
        Tk.CheckBox {
            objectName: "backgroundTile"
            visible: pane.bg.kind === "picture"
            small: true
            text: qsTr("Tile picture as texture")
            checked: pane.bg.tile
            onToggled: pane.bg.setTile(checked)
        }

        // ---- pattern ----
        Tk.PropertyRow {
            visible: pane.bg.kind === "pattern"
            label: qsTr("Pattern")
            dense: true
            Tk.ComboBox {
                objectName: "backgroundPattern"
                small: true
                model: pane.bg.patterns.map(function(p) { return pane.patternNames[p] || p })
                currentIndex: Math.max(0, pane.bg.patterns.indexOf(pane.bg.pattern))
                onActivated: function(index) {
                    pane.bg.setPattern(pane.bg.patterns[index], pane.bg.patternForeground, pane.bg.patternBackground)
                }
            }
        }
        Tk.PropertyRow {
            visible: pane.bg.kind === "pattern"
            label: qsTr("Foreground")
            dense: true
            Tk.ColorField {
                objectName: "backgroundPatternForeground"
                small: true
                color: pane.bg.patternForeground
                onColorEdited: function(color) { pane.applyColor("foreground", color) }
                onPickRequested: pane.pick("foreground", pane.bg.patternForeground)
            }
        }
        Tk.PropertyRow {
            visible: pane.bg.kind === "pattern"
            label: qsTr("Background")
            dense: true
            Tk.ColorField {
                objectName: "backgroundPatternBackground"
                small: true
                color: pane.bg.patternBackground
                onColorEdited: function(color) { pane.applyColor("background", color) }
                onPickRequested: pane.pick("background", pane.bg.patternBackground)
            }
        }

        // ---- every kind ----
        Tk.PropertyRow {
            label: qsTr("Transparency")
            dense: true
            Tk.Slider {
                id: transparency
                objectName: "backgroundTransparency"
                small: true
                from: 0
                to: 100
                stepSize: 1
                showValue: true
                suffix: "%"
                value: pane.bg.transparency
                tooltip: qsTr("Transparency")
                // One undo step per gesture: on release, or per key press.
                onPressedChanged: if (!pressed) pane.bg.setTransparency(Math.round(value))
                onValueModified: function(value) { if (!pressed) pane.bg.setTransparency(Math.round(value)) }
            }
        }
        Tk.Flex {
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "backgroundApplyToAll"
                small: true
                text: qsTr("Apply to All")
                tooltip: qsTr("Every slide takes this background (the slide master keeps it)")
                onClicked: pane.showHost.actions.trigger("showBackgroundApplyToAllAction")
            }
            Tk.Button {
                objectName: "backgroundReset"
                small: true
                text: qsTr("Reset Background")
                enabled: pane.bg.target !== "master" && pane.bg.own
                onClicked: pane.showHost.actions.trigger("showBackgroundResetAction")
            }
        }
    }
    Dialogs.ColorDialog {
        id: colorDialog
        title: qsTr("Background colour")
        onAccepted: pane.applyColor(pane.colorTarget, selectedColor)
    }
    Dialogs.FileDialog {
        id: pictureDialog
        title: qsTr("Insert picture")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff)"), qsTr("All files (*)")]
        onAccepted: pane.bg.setPictureFile(selectedFile)
    }
    Connections {
        target: pane.bg
        function onPictureRequested() { pictureDialog.open() }
    }
}
