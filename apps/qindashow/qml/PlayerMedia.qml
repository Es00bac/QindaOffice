// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Show

// The slide show's audio and video (M75, D-084), over the player's frames:
// one MediaSurface (loaded at run time, media/MediaSurface.qml) per medium
// of the current slide, placed where the letterboxed slide shows its
// shape. A medium set to Start Automatically plays with the slide, an On
// Click one at its click (its build step, the click sequence PowerPoint
// writes); Loop until Stopped loops; Hide While Not Playing keeps it out
// of sight until it plays (the renderer leaves its poster out). A click
// on a video plays or pauses it (toggleAt, asked by the player's pointer
// before it advances). Changing slides stops everything.
//
// M91 (D-099): a medium plays from its trimmed start to its trimmed end,
// fading in and out (MediaSurface's startMs / endMs / fadeInMs /
// fadeOutMs), and jumpBookmark(±1) — Alt+End / Alt+Home in the player —
// seeks the medium playing to its next or previous bookmark.
//
// AGENT-NOTE: without Qt Multimedia (ShowMedia.available false, or
// QQO_SHOW_MEDIA=off in the tests) nothing loads: the poster frames the
// renderer paints are the whole show, and the tests stay silent.
Item {
    // AGENT-GUARD: not `layer` — every Item has a `layer` property, which
    // would shadow the id inside the delegates (undefined showHost).
    id: stage

    required property ShowHost showHost
    property PresentationController controller: null
    // The player is up (nothing plays behind a hidden window).
    property bool showing: false

    readonly property size slideSize: showHost.design.slideSize
    readonly property real slideScale: slideSize.width > 0 && slideSize.height > 0
                                       ? Math.min(width / slideSize.width, height / slideSize.height) : 1
    readonly property real offsetX: (width - slideSize.width * slideScale) / 2
    readonly property real offsetY: (height - slideSize.height * slideScale) / 2
    readonly property int slide: controller !== null ? controller.slide : -1
    readonly property int build: controller !== null ? controller.build : 0
    readonly property int count: media.count

    objectName: "playerMedia"

    // The next (+1) or previous (-1) bookmark of the medium playing; true
    // when there was one to go to.
    function jumpBookmark(direction) {
        for (let i = 0; i < media.count; ++i) {
            const box = media.itemAt(i)
            const surface = box !== null ? box.surface : null
            if (surface === null || !box.started || box.modelData.bookmarks.length === 0 || surface.seek === undefined) continue
            const here = surface.position !== undefined ? surface.position : 0
            const marks = box.modelData.bookmarks
            let target = -1
            if (direction > 0) {
                for (let m = 0; m < marks.length && target < 0; ++m) if (marks[m] > here + 250) target = marks[m]
            } else {
                for (let m = marks.length - 1; m >= 0 && target < 0; --m) if (marks[m] < here - 1000) target = marks[m]
            }
            if (target >= 0) {
                surface.seek(target)
                return true
            }
        }
        return false
    }

    // A click at (x, y) on a video plays or pauses it; true when it did.
    function toggleAt(x, y) {
        for (let i = 0; i < media.count; ++i) {
            const box = media.itemAt(i)
            if (box !== null && box.visible && box.isVideo && box.contains(box.mapFromItem(stage, x, y))) {
                box.toggle()
                return true
            }
        }
        return false
    }

    Repeater {
        id: media
        model: stage.showing && stage.showHost.media.available && stage.slide >= 0
               ? stage.showHost.media.slideMedia(stage.slide) : []

        Item {
            id: box
            required property var modelData
            required property int index
            readonly property bool isVideo: modelData.kind === "video"
            // Due: automatically with the slide, else from its click on.
            readonly property bool due: modelData.autoPlay || stage.build >= modelData.step
            property bool clicked: false
            property bool paused: false
            property bool ended: false
            readonly property bool started: loader.item !== null && loader.item.started
            readonly property var surface: loader.item

            function toggle() {
                if (!due && !clicked) {
                    clicked = true
                } else if (ended) {
                    // Rewind first: clearing `ended` plays at once (the Binding).
                    if (loader.item !== null) loader.item.stop()
                    paused = false
                    ended = false
                } else {
                    paused = !paused
                }
            }

            objectName: "playerMedia_" + index
            x: stage.offsetX + modelData.x * stage.slideScale
            y: stage.offsetY + modelData.y * stage.slideScale
            width: modelData.width * stage.slideScale
            height: modelData.height * stage.slideScale
            visible: !modelData.hideWhenStopped || started

            Loader {
                id: loader
                anchors.fill: parent
                source: stage.showHost.media.surface
                onLoaded: {
                    // M91: trim and fades first — the source starts playing.
                    // (A stand-in surface may not know them: tested before set.)
                    const m = box.modelData
                    if ("startMs" in item) item.startMs = m.startMs
                    if ("endMs" in item) item.endMs = m.endMs
                    if ("trimEndMs" in item) item.trimEndMs = m.trimEndMs
                    if ("fadeInMs" in item) item.fadeInMs = m.fadeInMs
                    if ("fadeOutMs" in item) item.fadeOutMs = m.fadeOutMs
                    item.source = m.url
                    item.video = box.isVideo
                    item.loop = m.loop
                    item.volume = m.volume / 100
                }
            }
            Binding {
                target: loader.item
                property: "playing"
                value: (box.due || box.clicked) && !box.paused && !box.ended
                when: loader.item !== null
            }
            Connections {
                target: loader.item
                ignoreUnknownSignals: true
                function onFinished() { box.ended = true }
            }
        }
    }
}
