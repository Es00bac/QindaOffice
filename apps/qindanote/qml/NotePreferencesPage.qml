// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// Settings ▸ Preferences ▸ QindaNote (D-014: every key is READ by the page
// item — pressure per sample, smoothing in the stroke builder).
Tk.Flex {
    id: page
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function apply() {
        Preferences.notePressureEnabled = pressure.checked
        Preferences.noteTouchDrawing = touchDrawing.checked
        Preferences.noteSmoothing = ["off", "light", "strong"][smoothing.currentIndex]
    }
    Component.onCompleted: {
        pressure.checked = Preferences.notePressureEnabled
        touchDrawing.checked = Preferences.noteTouchDrawing
        smoothing.currentIndex = Math.max(0, ["off", "light", "strong"].indexOf(Preferences.noteSmoothing))
    }

    Tk.PropertyRow {
        label: qsTr("Pen")
        Tk.Switch { id: pressure; objectName: "prefNotePressure"; text: qsTr("Pen pressure changes the width") }
    }
    Tk.PropertyRow {
        label: qsTr("Touch")
        hint: qsTr("Off: one finger scrolls the page. Two fingers always zoom and scroll.")
        Tk.Switch { id: touchDrawing; objectName: "prefNoteTouchDrawing"; text: qsTr("Draw with a finger") }
    }
    Tk.PropertyRow {
        label: qsTr("Smoothing")
        hint: qsTr("Stored samples are never changed; smoothing is a render-time filter.")
        Tk.ComboBox { id: smoothing; objectName: "prefNoteSmoothing"; model: [qsTr("Off"), qsTr("Light"), qsTr("Strong")] }
    }
}
