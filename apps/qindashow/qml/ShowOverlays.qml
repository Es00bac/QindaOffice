// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// What the slide show adds over its slide (M91, D-099), for the player and
// the presenter view alike: an action's highlight on the clicked shape, a
// notice (a program or macro not run, a link refused), the black "End of
// slide show" screen, the question before another file opens, and the
// sounds actions play (through MediaSurface.qml when Qt Multimedia is
// there, else silently skipped).
//
// AGENT-NOTE: the highlight, the end screen and the notice draw over the
// slide picture — show surfaces, like the black around a letterboxed
// slide; the notice and the question are QindaTK chrome.
Item {
    id: overlays
    required property ShowHost showHost
    property PresentationController controller: null
    // The slide's place in this item: offset and scale (slide points → here).
    property real slideX: 0
    property real slideY: 0
    property real slideScale: 1
    // This window asks the questions (the player, or the presenter alone).
    property bool handlesPrompts: false
    property bool endScreen: false
    property string notice: ""
    property rect highlight: Qt.rect(0, 0, 0, 0)

    objectName: "showOverlays"

    function say(text) {
        overlays.notice = text
        noticeTimer.restart()
    }

    readonly property Tk.MessageDialog consent: Tk.MessageDialog {
        objectName: "openFileDialog"
        hostItem: overlays
        property string path: ""
        title: qsTr("Open a File")
        variant: "question"
        text: qsTr("This slide links to %1. Open it?").arg(path)
        primaryText: qsTr("Open")
        onAccepted: overlays.showHost.openFileConfirmed(path)
    }
    readonly property list<QtObject> watchers: [
        Connections {
            target: overlays.controller
            function onNoticeRequested(text) { overlays.say(text) }
            function onHighlightRequested(rect) { overlays.highlight = rect; highlightTimer.restart() }
            function onSoundRequested(url) {
                // Exactly one window owns external effects: the player, or
                // the presenter when it is the only visible show surface.
                if (!overlays.handlesPrompts || !overlays.showHost.media.available) return
                sound.active = false
                sound.url = url
                sound.active = true
            }
            function onStopSoundsRequested() { sound.active = false }
        },
        Connections {
            target: overlays.showHost
            function onShowNotice(text) { if (overlays.visible) overlays.say(text) }
            function onFileConsentRequested(path) {
                if (overlays.handlesPrompts) {
                    overlays.consent.path = path
                    overlays.consent.open()
                }
            }
        },
        Timer { id: noticeTimer; interval: 5000; onTriggered: overlays.notice = "" },
        Timer { id: highlightTimer; interval: 250; onTriggered: overlays.highlight = Qt.rect(0, 0, 0, 0) }
    ]

    // The clicked shape flashes (Highlight click).
    Rectangle {
        objectName: "showHighlight"
        visible: overlays.highlight.width > 0
        x: overlays.slideX + overlays.highlight.x * overlays.slideScale
        y: overlays.slideY + overlays.highlight.y * overlays.slideScale
        width: overlays.highlight.width * overlays.slideScale
        height: overlays.highlight.height * overlays.slideScale
        color: Qt.rgba(1, 1, 1, 0.35)
        border.color: Qt.rgba(0, 0, 0, 0.6)
        border.width: 2
    }
    // PowerPoint's end of the show: black, one click to leave.
    Rectangle {
        objectName: "showEndScreen"
        anchors.fill: parent
        visible: overlays.endScreen
        color: "black"
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.08
            color: "white"
            font.pixelSize: Math.max(12, parent.height / 30)
            text: qsTr("End of slide show, click to exit.")
        }
    }
    Tk.Notice {
        objectName: "showNotice"
        visible: overlays.notice.length > 0
        x: (overlays.width - width) / 2
        y: overlays.height - height - Tk.Theme.space.xl
        width: Math.min(overlays.width - Tk.Theme.space.xl * 2, 560)
        variant: "warning"
        text: overlays.notice
    }
    Loader {
        id: sound
        objectName: "showActionSound"
        property url url
        active: false
        visible: false
        source: overlays.showHost.media.surface
        onLoaded: {
            item.video = false
            item.source = sound.url
            item.playing = true
        }
        Connections {
            target: sound.item
            ignoreUnknownSignals: true
            function onFinished() { sound.active = false }
        }
    }
}
