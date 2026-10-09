// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The current slide's transition and layout (its speaker notes are in the
// notes pane under the slide since M67, NotesPane.qml).
Tk.Flex {
    id: panel
    required property ShowHost showHost
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
    Tk.PropertyRow {
        label: qsTr("Layout")
        Tk.ComboBox {
            objectName: "slideLayoutCombo"
            model: panel.showHost.layoutNames
            currentIndex: panel.showHost.layoutIndex
            onActivated: function(index) { panel.showHost.layoutIndex = index }
        }
    }
}
