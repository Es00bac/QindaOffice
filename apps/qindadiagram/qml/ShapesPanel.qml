// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The Shapes panel (D-050): every stencil as a thumbnail the engine's own
// painter drew in the drawing's theme, in a Tk.ShapePalette (QindaTK D-302:
// tiles, a category menu, search across stencils, drag reports). A click
// drops the shape in the middle of the view; a drag drops it where it is
// released (the window maps the scene point onto the page).
Tk.Box {
    id: panel
    objectName: "diagramShapesPanel"

    required property DiagramService diagram
    readonly property alias shapePalette: palette

    signal shapeRequested(string stencilId)
    signal dragMoved(string stencilId, point scenePoint)
    signal dragFinished(string stencilId, point scenePoint)
    signal dragCanceled()

    color: Tk.Theme.color.panel
    padding: Tk.Theme.space.md

    Tk.Scroll {
        id: scroll
        objectName: "diagramShapesScroll"
        overflowY: Tk.Scroll.Auto
        Tk.ShapePalette {
            id: palette
            objectName: "diagramShapePalette"
            width: scroll.viewportWidth
            definitions: panel.diagram.stencils
            columns: 3
            categoryPicker: "menu"
            searchAllCategories: true
            dragEnabled: true
            tileSize: Tk.Theme.size.thumbnailHeight
            category: "Flowchart"
            tooltip: qsTr("Stencils: drag a shape onto the page, or click to drop it in the middle")
            onShapeRequested: function(shapeId) { panel.shapeRequested(shapeId) }
            onShapeDragMoved: function(shapeId, scenePoint) { panel.dragMoved(shapeId, scenePoint) }
            onShapeDragFinished: function(shapeId, parameters, scenePoint) { panel.dragFinished(shapeId, scenePoint) }
            onShapeDragCanceled: panel.dragCanceled()
        }
    }
}
