// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The current slide's transition, layout and speaker notes.
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
    Tk.Overline { text: qsTr("Speaker notes") }
    Tk.Scroll {
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        Tk.TextArea {
            id: notes
            objectName: "slideNotes"
            width: parent.width
            rows: 6
            placeholderText: qsTr("What to say on this slide")
            text: panel.showHost.notes
            onTextChanged: if (activeFocus) panel.showHost.notes = text
        }
    }
}
