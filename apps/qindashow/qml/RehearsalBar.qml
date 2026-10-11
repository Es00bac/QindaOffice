// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The Recording toolbar of Rehearse Timings (M91, D-099), at the top left
// of the show as in PowerPoint: Next, Pause / Resume, the current slide's
// time, Repeat (that slide's clock starts over), and the whole rehearsal's
// time (ShowRehearsal). At the end the window asks whether to keep the
// timings (ShowSlideShow::keepTimingsRequested).
Tk.Box {
    id: bar
    property ShowRehearsal rehearsal: null
    signal nextRequested()
    signal repeatRequested()

    objectName: "rehearsalBar"
    visible: rehearsal !== null
    padding: Tk.Theme.space.xs
    color: Tk.Theme.color.surface
    radius: Tk.Theme.radius.md
    borderWidth: Tk.Theme.size.border
    borderColor: Tk.Theme.color.border

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.Caption { text: qsTr("Recording") }
        Tk.IconButton {
            objectName: "rehearsalNext"
            iconName: "arrow-right"
            tooltip: qsTr("Next")
            onClicked: bar.nextRequested()
        }
        Tk.IconButton {
            objectName: "rehearsalPause"
            iconName: bar.rehearsal !== null && bar.rehearsal.paused ? "play" : "pause"
            tooltip: bar.rehearsal !== null && bar.rehearsal.paused ? qsTr("Resume Recording") : qsTr("Pause Recording")
            onClicked: if (bar.rehearsal !== null) bar.rehearsal.togglePause()
        }
        Tk.Mono {
            objectName: "rehearsalSlideTime"
            text: bar.rehearsal !== null ? bar.rehearsal.slideText : ""
        }
        Tk.IconButton {
            objectName: "rehearsalRepeat"
            iconName: "rotate-ccw"
            tooltip: qsTr("Repeat")
            onClicked: if (bar.rehearsal !== null) { bar.rehearsal.repeat(); bar.repeatRequested() }
        }
        Tk.Mono {
            objectName: "rehearsalTotalTime"
            text: bar.rehearsal !== null ? bar.rehearsal.totalText : ""
        }
    }
}
