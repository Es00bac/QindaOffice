// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Show

// The player (plan §5.3): a fullscreen window driven by the host's
// PresentationController. Two SlideFrameItems — the frozen outgoing
// frame under the live incoming one — animate the slide's transition
// (cut / fade / push — the model's "slide" — / zoom, and since M67 wipe
// from the right and split vertical out, both revealing the incoming frame
// through a clipping item; 350 ms OutCubic, none under reduced motion); a
// build step animates the arriving shapes through buildProgress. Keys:
// Space/arrows advance, Backspace back, B/W blank, P pen and L laser
// annotations (ephemeral, kept per slide for the session), E erases, Esc
// ends the show. Left click advances, right click goes back, middle ends.
// The slide's audio and video play over the frames (PlayerMedia.qml, M75);
// a left click on a video plays or pauses it instead of advancing.
//
// M91 (D-099): Set Up Slide Show — a window for "browsed by an individual",
// a kiosk that only actions, timings and Esc move (and that starts over
// when left alone), the pen and laser colours; a click on an action runs
// it (the pointer is a hand over one, the mouse passing over runs a
// mouse-over action); the slides' timings advance by themselves; the end
// shows PowerPoint's black "End of slide show" screen; Alt+End / Alt+Home
// jump between a playing medium's bookmarks; Rehearse Timings shows the
// Recording toolbar. Highlights, notices, sounds and the question before a
// file opens are ShowOverlays.qml.
Window {
    id: player
    required property ShowHost showHost
    property PresentationController controller: null
    // The window sets this while the host has a running show.
    property bool active: false
    signal ended()
    objectName: "showPlayer"
    color: "black"
    readonly property bool windowed: controller !== null && controller.windowed
    visibility: active ? (windowed ? Window.Windowed : Window.FullScreen) : Window.Hidden
    flags: windowed ? Qt.Window : Qt.Window | Qt.FramelessWindowHint
    title: qsTr("Slide Show")
    width: 960
    height: 540

    property int lastSlide: -1
    property int lastBuild: 0
    property bool blank: false
    property color blankColor: "black"
    property string tool: "none"    // "none" | "pen" | "laser"
    property var strokesBySlide: ({})
    property var strokes: []
    property var activeStroke: []
    property point laserPos: Qt.point(-100, -100)
    property bool laserVisible: false
    property bool overAction: false
    property bool pointerShown: false
    readonly property int transitionMs: showHost.reducedMotion ? 0 : 350
    // How much of the incoming frame a wipe or split shows (M67).
    property string revealKind: ""
    property real revealProgress: 1
    // Where the letterboxed slide is (slide points → window).
    readonly property size slideSize: showHost.design.slideSize
    readonly property real slideScale: slideSize.width > 0 && slideSize.height > 0
                                       ? Math.min(width / slideSize.width, height / slideSize.height) : 1
    readonly property real slideX: (width - slideSize.width * slideScale) / 2
    readonly property real slideY: (height - slideSize.height * slideScale) / 2

    function toSlide(x, y) { return Qt.point((x - slideX) / slideScale, (y - slideY) / slideScale) }

    SlideFrameItem {
        id: outgoing
        objectName: "playerOutgoing"
        anchors.fill: parent
        controller: player.controller
        visible: transition.running
        transform: Translate { id: slideOut; x: 0 }
    }
    Item {
        id: reveal
        objectName: "playerReveal"
        clip: player.revealKind !== ""
        x: player.revealKind === "wipe" ? player.width * (1 - player.revealProgress)
           : player.revealKind === "split" ? player.width * (1 - player.revealProgress) / 2 : 0
        y: 0
        width: player.revealKind === "" ? player.width : player.width * player.revealProgress
        height: player.height
        SlideFrameItem {
            id: incoming
            objectName: "playerIncoming"
            x: -reveal.x
            y: 0
            width: player.width
            height: player.height
            controller: player.controller
            buildProgress: 1.0
            transform: [Translate { id: slideIn; x: 0 }, Scale { id: zoomIn; origin.x: incoming.width / 2; origin.y: incoming.height / 2; xScale: 1; yScale: 1 }]
        }
    }
    NumberAnimation {
        id: transition
        property string kind: "cut"
        target: incoming
        property: "opacity"
        // Fade and zoom blend in; push, wipe and split move or reveal a whole frame.
        from: kind === "fade" || kind === "zoom" ? 0 : 1
        to: 1
        duration: player.transitionMs
        easing.type: Easing.OutCubic
        onStarted: {
            slideIn.x = kind === "slide" ? player.width : 0
            zoomIn.xScale = kind === "zoom" ? 1.15 : 1
            zoomIn.yScale = zoomIn.xScale
            player.revealKind = kind === "wipe" || kind === "split" ? kind : ""
            player.revealProgress = player.revealKind !== "" ? 0 : 1
        }
        onStopped: {
            incoming.opacity = 1; slideIn.x = 0; slideOut.x = 0; zoomIn.xScale = 1; zoomIn.yScale = 1
            player.revealKind = ""; player.revealProgress = 1; outgoing.frozen = false
        }
    }
    NumberAnimation { id: slideMove; target: slideIn; property: "x"; to: 0; duration: player.transitionMs; easing.type: Easing.OutCubic }
    NumberAnimation { id: pushOut; target: slideOut; property: "x"; from: 0; to: -player.width; duration: player.transitionMs; easing.type: Easing.OutCubic }
    NumberAnimation { id: revealMove; target: player; property: "revealProgress"; from: 0; to: 1; duration: player.transitionMs; easing.type: Easing.OutCubic }
    NumberAnimation { id: zoomMoveX; target: zoomIn; property: "xScale"; to: 1; duration: player.transitionMs; easing.type: Easing.OutCubic }
    NumberAnimation { id: zoomMoveY; target: zoomIn; property: "yScale"; to: 1; duration: player.transitionMs; easing.type: Easing.OutCubic }
    NumberAnimation {
        id: buildAnimation
        target: incoming
        property: "buildProgress"
        from: 0
        to: 1
        easing.type: Easing.OutCubic
    }
    // M91: the slide's timing, and a kiosk left alone.
    Timer {
        id: autoAdvance
        objectName: "playerAutoAdvance"
        interval: player.controller !== null ? Math.max(1, player.controller.autoAdvanceMs) : 1000
        running: player.active && player.controller !== null && player.controller.autoAdvanceMs >= 0 && !player.blank && !overlays.endScreen
        onTriggered: player.advanceTimed()
    }
    Timer {
        id: kioskIdle
        interval: player.controller !== null ? Math.max(1000, player.controller.kioskRestartMs) : 300000
        running: player.active && player.controller !== null && player.controller.kiosk
        onTriggered: if (player.controller !== null) player.controller.restart()
    }
    Timer { id: pointerIdle; interval: 3000; onTriggered: player.pointerShown = false }

    function onStateChanged() {
        if (controller === null) return
        const slide = controller.slide
        const build = controller.build
        player.strokes = player.strokesBySlide[slide] || []
        player.activeStroke = []
        player.laserVisible = false
        player.blank = false
        overlays.endScreen = false
        // A manual show and a rehearsal must never acquire a running timer.
        if (player.active && controller.autoAdvanceMs >= 0) autoAdvance.restart()
        else autoAdvance.stop()
        if (slide === player.lastSlide && build > player.lastBuild) {
            // A build step is not a slide change: animate the arriving
            // shapes instead of blending two whole frames.
            const duration = controller.buildDurationMs
            if (!player.showHost.reducedMotion && duration > 0) {
                buildAnimation.duration = duration
                buildAnimation.restart()
            } else {
                incoming.buildProgress = 1
            }
        } else if (player.lastSlide >= 0 && player.transitionMs > 0) {
            transition.kind = controller.transition
            if (transition.kind === "cut") {
                incoming.opacity = 1
            } else {
                transition.restart()
                if (transition.kind === "slide") { slideMove.restart(); pushOut.restart() }
                if (transition.kind === "zoom") { zoomMoveX.restart(); zoomMoveY.restart() }
                if (transition.kind === "wipe" || transition.kind === "split") revealMove.restart()
            }
        }
        player.lastSlide = slide
        player.lastBuild = build
        ink.requestPaint()
    }
    function advance(delta) {
        if (controller === null) return
        kioskIdle.restart()
        if (overlays.endScreen) {
            if (delta > 0) player.ended()
            else overlays.endScreen = false
            return
        }
        player.keepInk()
        controller.advance(delta)
    }
    // M91: the slide's timing ran out (a kiosk moves only so, or by actions).
    function advanceTimed() {
        if (controller === null || overlays.endScreen) return
        player.keepInk()
        controller.timedAdvance()
    }
    // Snapshot the outgoing frame and its ink before the controller moves.
    function keepInk() {
        if (player.activeStroke.length > 1) player.strokes = player.strokes.concat([player.activeStroke])
        player.activeStroke = []
        const table = player.strokesBySlide
        table[controller.slide] = player.strokes
        player.strokesBySlide = table
        outgoing.freezeCurrent()
    }
    onControllerChanged: {
        player.lastSlide = -1
        player.lastBuild = 0
        player.strokesBySlide = ({})
        player.strokes = []
        overlays.endScreen = false
        if (controller !== null) player.onStateChanged()
    }
    Connections {
        target: player.controller
        function onStateChanged() { player.onStateChanged() }
        function onEndReached() { overlays.endScreen = true }
    }
    PlayerMedia {
        id: playerMedia
        anchors.fill: parent
        z: 4
        showHost: player.showHost
        controller: player.controller
        showing: player.active
        opacity: transition.running ? 0 : 1
    }
    Rectangle { anchors.fill: parent; visible: player.blank; color: player.blankColor; z: 5 }
    Canvas {
        id: ink
        objectName: "playerInk"
        anchors.fill: parent
        z: 6
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            // M91: the pen and the laser in the colours Set Up chose.
            ctx.strokeStyle = player.controller !== null ? String(player.controller.penColor) : "#ff0000"
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const all = player.strokes.concat(player.activeStroke.length > 0 ? [player.activeStroke] : [])
            for (let s = 0; s < all.length; ++s) {
                const stroke = all[s]
                if (stroke.length < 2) continue
                ctx.beginPath()
                ctx.moveTo(stroke[0].x, stroke[0].y)
                for (let i = 1; i < stroke.length; ++i) ctx.lineTo(stroke[i].x, stroke[i].y)
                ctx.stroke()
            }
            if (player.laserVisible && player.tool === "laser") {
                ctx.fillStyle = player.controller !== null ? String(player.controller.laserColor) : "#ff0000"
                ctx.beginPath()
                ctx.arc(player.laserPos.x, player.laserPos.y, 5, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        z: 7
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        cursorShape: player.overAction ? Qt.PointingHandCursor
                   : player.tool !== "none" ? Qt.CrossCursor
                   : player.pointerShown || player.windowed ? Qt.ArrowCursor : Qt.BlankCursor
        onPressed: function(mouse) {
            kioskIdle.restart()
            if (mouse.button === Qt.LeftButton && player.tool === "pen") { player.activeStroke = [Qt.point(mouse.x, mouse.y)]; return }
            if (mouse.button === Qt.LeftButton && overlays.endScreen) { player.ended(); return }
            if (mouse.button === Qt.LeftButton && playerMedia.toggleAt(mouse.x, mouse.y)) return
            if (mouse.button === Qt.LeftButton) {
                const at = player.toSlide(mouse.x, mouse.y)
                if (player.controller === null) return
                player.keepInk() // an action may move to another slide
                if (player.controller.click(at.x, at.y)) return
                if (player.controller !== null && player.controller.clickAdvances) player.advance(1)
            } else if (mouse.button === Qt.RightButton) {
                if (player.controller !== null && !player.controller.kiosk) player.advance(-1)
            } else {
                player.ended()
            }
        }
        onPositionChanged: function(mouse) {
            kioskIdle.restart()
            player.pointerShown = true
            pointerIdle.restart()
            const at = player.toSlide(mouse.x, mouse.y)
            if (player.controller !== null && !overlays.endScreen && player.tool !== "pen" && mouse.buttons === 0) {
                outgoing.freezeCurrent() // a mouse-over action may move to another slide
                player.overAction = player.controller.hover(at.x, at.y)
            } else {
                player.overAction = false
            }
            if (player.tool === "pen" && player.activeStroke.length > 0 && (mouse.buttons & Qt.LeftButton)) {
                player.activeStroke = player.activeStroke.concat([Qt.point(mouse.x, mouse.y)])
                ink.requestPaint()
            } else if (player.tool === "laser") {
                player.laserPos = Qt.point(mouse.x, mouse.y)
                player.laserVisible = true
                ink.requestPaint()
            }
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.LeftButton && player.tool === "pen" && player.activeStroke.length > 1) {
                player.strokes = player.strokes.concat([player.activeStroke])
                player.activeStroke = []
                ink.requestPaint()
            }
        }
    }
    ShowOverlays {
        id: overlays
        anchors.fill: parent
        z: 8
        showHost: player.showHost
        controller: player.controller
        slideX: player.slideX
        slideY: player.slideY
        slideScale: player.slideScale
        handlesPrompts: player.visible
    }
    RehearsalBar {
        x: 16
        y: 16
        z: 9
        rehearsal: player.controller !== null ? player.controller.rehearsal : null
        onNextRequested: player.advance(1)
        onRepeatRequested: if (player.controller !== null) player.controller.goTo(player.controller.slide, 0)
    }
    Item {
        focus: true
        Keys.onPressed: function(event) {
            kioskIdle.restart()
            const kiosk = player.controller !== null && player.controller.kiosk
            if ((event.modifiers & Qt.AltModifier) && (event.key === Qt.Key_End || event.key === Qt.Key_Home)) {
                playerMedia.jumpBookmark(event.key === Qt.Key_End ? 1 : -1)
                event.accepted = true
                return
            }
            switch (event.key) {
            case Qt.Key_Escape: case Qt.Key_Q: player.ended(); break
            case Qt.Key_Space: case Qt.Key_Right: case Qt.Key_PageDown: case Qt.Key_Down: case Qt.Key_Return: case Qt.Key_Enter:
                if (!kiosk) player.advance(1)
                break
            case Qt.Key_Left: case Qt.Key_PageUp: case Qt.Key_Up: case Qt.Key_Backspace:
                if (!kiosk) player.advance(-1)
                break
            case Qt.Key_B: player.blank = true; player.blankColor = "black"; break
            case Qt.Key_W: player.blank = true; player.blankColor = "white"; break
            case Qt.Key_P: player.tool = player.tool === "pen" ? "none" : "pen"; break
            case Qt.Key_L: player.tool = player.tool === "laser" ? "none" : "laser"; break
            case Qt.Key_E: player.strokes = []; player.activeStroke = []; ink.requestPaint(); break
            default: event.accepted = false; return
            }
            event.accepted = true
        }
    }
    onVisibleChanged: if (visible) requestActivate()
}
