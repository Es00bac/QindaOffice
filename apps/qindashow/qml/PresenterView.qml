// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The speaker view on the second screen while the player owns the
// projector — or alone on the only screen with Slide Show ▸ Presenter View
// (M67) — the current slide, the next-slide preview, the speaker notes, an
// elapsed timer, the wall clock and the slide counter. Observes the same
// PresentationController as the player; Escape ends the show.
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
            SlideFrameItem { objectName: "presenterCurrent"; controller: presenter.controller; implicitWidth: 320; implicitHeight: 180; Tk.Flex.grow: 3; Tk.Flex.basis: 0 }
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
                    slideIndex: presenter.controller !== null && presenter.controller.slide + 1 < presenter.controller.slideCount ? presenter.controller.slide + 1 : -2
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
            Tk.Label { objectName: "presenterCounter"; text: presenter.controller !== null ? qsTr("Slide %1 / %2").arg(presenter.controller.slide + 1).arg(presenter.controller.slideCount) : "" }
            Tk.Mono { id: clock; text: Qt.formatTime(new Date(), "hh:mm") }
        }
    }
    Item {
        focus: true
        Keys.onEscapePressed: presenter.ended()
        Keys.onSpacePressed: if (presenter.controller !== null) presenter.controller.advance(1)
    }
}
