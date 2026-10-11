// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The speaker view on the second screen while the player owns the
// projector — or alone on the only screen with Slide Show ▸ Presenter View
// (M67) — the current slide, the next-slide preview, the speaker notes, an
// elapsed timer, the wall clock and the slide counter. Observes the same
// PresentationController as the player; Escape ends the show.
// M91 (D-099): the next slide is the show's (its order, a custom show,
// looping); a click on the current slide runs the action there; alone on
// the screen it asks before a file opens and shows the notices
// (ShowOverlays.qml); the counter counts the show's own slides.
Window {
    id: presenter
    required property ShowHost showHost
    property PresentationController controller: null
    // The window sets this while a show runs and a second screen exists.
    property bool active: false
    signal ended()
    objectName: "showPresenter"
    color: Tk.Theme.color.bg
    visibility: active ? Window.FullScreen : Window.Hidden
    width: 960
    height: 600
    property int startedAt: 0
    property int elapsedSeconds: 0
    onControllerChanged: { presenter.startedAt = Date.now(); presenter.elapsedSeconds = 0 }
    Timer {
        interval: 1000; repeat: true; running: presenter.visible
        onTriggered: { presenter.elapsedSeconds = Math.floor((Date.now() - presenter.startedAt) / 1000); clock.text = Qt.formatTime(new Date(), "hh:mm") }
    }
    function pad(n) { return (n < 10 ? "0" : "") + n }
    // AGENT-GUARD (M67): sized from the window, not anchored to its content
    // item — shown straight into full screen (offscreen, and on some
    // compositors) the content item can stay 0×0 and the whole view
    // collapsed into its corner.
    Tk.Flex {
        x: 0
        y: 0
        width: presenter.width
        height: presenter.height
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 2
            Tk.Flex.basis: 0
            SlideFrameItem {
                id: current
                objectName: "presenterCurrent"
                controller: presenter.controller
                implicitWidth: 320
                implicitHeight: 180
                Tk.Flex.grow: 3
                Tk.Flex.basis: 0
                readonly property size page: presenter.showHost.design.slideSize
                readonly property real slideScale: page.width > 0 ? Math.min(width / page.width, height / page.height) : 1
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: overAction ? Qt.PointingHandCursor : Qt.ArrowCursor
                    property bool overAction: false
                    function slidePoint(mx, my) {
                        return Qt.point((mx - (width - current.page.width * current.slideScale) / 2) / current.slideScale,
                                        (my - (height - current.page.height * current.slideScale) / 2) / current.slideScale)
                    }
                    onClicked: function(mouse) {
                        if (presenter.controller === null) return
                        const at = slidePoint(mouse.x, mouse.y)
                        if (!presenter.controller.click(at.x, at.y) && presenter.controller.clickAdvances) presenter.advance(1)
                    }
                    onPositionChanged: function(mouse) {
                        const at = slidePoint(mouse.x, mouse.y)
                        overAction = presenter.controller !== null && presenter.controller.hover(at.x, at.y)
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex.grow: 2
                Tk.Flex.basis: 0
                Tk.Overline { text: qsTr("Next") }
                SlideFrameItem {
                    id: next
                    objectName: "presenterNext"
                    controller: presenter.controller
                    slideIndex: presenter.controller !== null && presenter.controller.nextSlide >= 0 ? presenter.controller.nextSlide : -2
                    buildStep: 1000
                    implicitWidth: 200
                    implicitHeight: 112
                    Tk.Flex.grow: 1
                }
                Tk.Label { objectName: "presenterEnd"; visible: next.slideIndex < 0; text: qsTr("End of the show") }
            }
        }
        Tk.Scroll {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Label { objectName: "presenterNotes"; width: parent.width; wrapMode: Text.Wrap; text: presenter.controller !== null ? presenter.controller.notes : "" }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.SpaceBetween
            Tk.Mono { objectName: "presenterTimer"; text: presenter.elapsedSeconds >= 3600
                    ? Math.floor(presenter.elapsedSeconds / 3600) + ":" + presenter.pad(Math.floor(presenter.elapsedSeconds % 3600 / 60)) + ":" + presenter.pad(presenter.elapsedSeconds % 60)
                    : presenter.pad(Math.floor(presenter.elapsedSeconds / 60)) + ":" + presenter.pad(presenter.elapsedSeconds % 60) }
            Tk.Label {
                objectName: "presenterCounter"
                text: presenter.controller === null ? ""
                    : presenter.controller.position > 0 ? qsTr("Slide %1 / %2").arg(presenter.controller.position).arg(presenter.controller.showLength)
                    : qsTr("Slide %1 / %2").arg(presenter.controller.slide + 1).arg(presenter.controller.slideCount)
            }
            Tk.Mono { id: clock; text: Qt.formatTime(new Date(), "hh:mm") }
        }
    }
    ShowOverlays {
        id: overlays
        x: 0
        y: 0
        width: presenter.width
        height: presenter.height
        showHost: presenter.showHost
        controller: presenter.controller
        handlesPrompts: presenter.visible && presenter.alone
        slideScale: current.slideScale
        slideX: current.mapToItem(presenter.contentItem, current.width / 2, current.height / 2).x - current.page.width * current.slideScale / 2
        slideY: current.mapToItem(presenter.contentItem, current.width / 2, current.height / 2).y - current.page.height * current.slideScale / 2
        endScreen: false
    }
    // Alone on the screen (Presenter View on one screen): past the end, the
    // next advance ends the show, as the player's end screen does.
    property bool alone: false
    property bool pastEnd: false
    function advance(delta) {
        if (presenter.controller === null || presenter.controller.kiosk) return
        if (presenter.pastEnd && delta > 0) { presenter.ended(); return }
        presenter.pastEnd = false
        presenter.controller.advance(delta)
    }
    Connections {
        target: presenter.controller
        function onEndReached() { presenter.pastEnd = true }
        function onStateChanged() {
            presenter.pastEnd = false
            if (presenter.active && presenter.alone && presenter.controller !== null && presenter.controller.autoAdvanceMs >= 0)
                advanceTimer.restart()
            else advanceTimer.stop()
        }
    }
    Timer {
        id: advanceTimer
        objectName: "presenterAutoAdvance"
        interval: presenter.controller !== null ? Math.max(1, presenter.controller.autoAdvanceMs) : 1000
        running: presenter.active && presenter.alone && presenter.controller !== null
                 && presenter.controller.autoAdvanceMs >= 0 && !presenter.pastEnd
        onTriggered: if (presenter.controller !== null) presenter.controller.timedAdvance()
    }
    Item {
        focus: true
        Keys.onEscapePressed: presenter.ended()
        Keys.onSpacePressed: presenter.advance(1)
        Keys.onRightPressed: presenter.advance(1)
        Keys.onLeftPressed: presenter.advance(-1)
    }
}
