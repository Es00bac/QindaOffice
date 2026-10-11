// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The current slide's transition and layout (its speaker notes are in the
// notes pane under the slide since M67, NotesPane.qml). M82 (D-091): the
// layout is chosen from the deck's own layouts, shown as thumbnails
// (LayoutGallery.qml) — the six slide kinds for a deck without layouts.
// M91 (D-099): the slide's Advance Slide (on a click, after N seconds).
Tk.Scroll {
    id: panel
    required property ShowHost showHost
    objectName: "slidePanel"
    // The field shows the current slide's timing (an edit breaks a binding).
    readonly property Connections advanceWatcher: Connections {
        target: panel.showHost.slideShow
        function onChanged() { afterField.value = Math.max(0, panel.showHost.slideShow.advanceSeconds) }
    }
    Tk.Flex {
        width: panel.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Transition")
            Tk.ComboBox {
                objectName: "slideTransitionCombo"
                model: panel.showHost.transitionNames
                currentIndex: panel.showHost.transitionIndex
                onActivated: function(index) { panel.showHost.transitionIndex = index }
            }
        }
        // M91 (D-099): Advance Slide — on a click, and after a time (the
        // rehearsed timing; written as the transition's advTm).
        Tk.Label { text: qsTr("Advance slide") }
        Tk.CheckBox {
            objectName: "slideAdvanceOnClick"
            checkable: false
            text: qsTr("On mouse click")
            checked: panel.showHost.slideShow.advanceOnClick
            onClicked: panel.showHost.slideShow.setAdvance(panel.showHost.slideShow.advanceSeconds, !checked)
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.CheckBox {
                objectName: "slideAdvanceAfter"
                checkable: false
                text: qsTr("After")
                checked: panel.showHost.slideShow.advanceSeconds >= 0
                onClicked: panel.showHost.slideShow.setAdvance(checked ? -1 : Math.max(0, afterField.value), panel.showHost.slideShow.advanceOnClick)
            }
            Tk.NumberField {
                id: afterField
                objectName: "slideAdvanceSeconds"
                enabled: panel.showHost.slideShow.advanceSeconds >= 0
                implicitWidth: 96
                from: 0
                to: 3600
                decimals: 2
                stepSize: 1
                scrub: false
                suffix: qsTr(" s")
                onValueModified: panel.showHost.slideShow.setAdvance(value, panel.showHost.slideShow.advanceOnClick)
            }
        }
        Tk.Label { text: qsTr("Layout") }
        LayoutGallery { showHost: panel.showHost }
    }
}
