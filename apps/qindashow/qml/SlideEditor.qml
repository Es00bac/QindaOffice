// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Show

// The editing area (M67, D-076): the slide, and under it the notes pane,
// with a splitter between them — PowerPoint's Normal view. View ▸ Notes
// (showViewNotesAction) shows and hides the pane.
Tk.Splitter {
    id: editor
    required property ShowHost showHost
    readonly property SlideCanvasView canvasView: canvasView
    objectName: "slideEditor"
    orientation: Qt.Vertical
    tooltip: qsTr("Resize the notes pane")
    SlideCanvasView {
        id: canvasView
        showHost: editor.showHost
        T.SplitView.fillHeight: true
        T.SplitView.minimumHeight: Tk.Theme.size.control * 4
    }
    NotesPane {
        showHost: editor.showHost
        // Notes belong to slides: none in the Slide Master view (M75).
        visible: editor.showHost.actions.entry("showViewNotesAction").checked && !editor.showHost.masterView.active
        T.SplitView.preferredHeight: Tk.Theme.size.control * 3.5
        T.SplitView.minimumHeight: Tk.Theme.size.control * 1.5
    }
}
