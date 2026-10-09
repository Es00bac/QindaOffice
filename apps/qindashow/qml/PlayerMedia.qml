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
                    item.source = box.modelData.url
                    item.video = box.isVideo
                    item.loop = box.modelData.loop
                    item.volume = box.modelData.volume / 100
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
