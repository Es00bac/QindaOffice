// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The current slide's transition and layout (its speaker notes are in the
// notes pane under the slide since M67, NotesPane.qml). M82 (D-091): the
// layout is chosen from the deck's own layouts, shown as thumbnails
// (LayoutGallery.qml) — the six slide kinds for a deck without layouts.
Tk.Scroll {
    id: panel
    required property ShowHost showHost
    objectName: "slidePanel"
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
        Tk.Label { text: qsTr("Layout") }
        LayoutGallery { showHost: panel.showHost }
    }
}
