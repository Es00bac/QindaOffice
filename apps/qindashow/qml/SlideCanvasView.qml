// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Show

// The slide inside a page-fitting viewport: the host's SlideCanvasItem is
// seated in `container` (slide points; the viewport's zoom is the only
// scale, D-009), the in-place editor is a TextEdit over the shape being
// edited showing the shape's own QTextDocument (runs and all), and a right
// press pops the arrange / group / clipboard context menu.
Tk.Viewport {
    id: viewport
    required property ShowHost showHost
    readonly property SlideCanvasItem canvasItem: showHost.canvas
    objectName: "showViewport"
    fitMode: "page"
    contentPadding: Tk.Theme.space.lg
    focus: true
    Item {
        id: container
        objectName: "slideContainer"
        implicitWidth: viewport.showHost.canvas.implicitWidth
        implicitHeight: viewport.showHost.canvas.implicitHeight
        Component.onCompleted: {
            viewport.showHost.attachCanvas(container)
            viewport.showHost.canvas.width = Qt.binding(function() { return container.width })
            viewport.showHost.canvas.height = Qt.binding(function() { return container.height })
            viewport.showHost.attachEditor(editor)
        }
        // Drop shadow and hairline frame: what makes the page read as a
        // sheet on the canvas rather than as the background.
        Rectangle { x: 4; y: 4; width: container.width; height: container.height; color: "#3c000000"; z: -1 }
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#787878"; border.width: 1; z: 1 }
        TextEdit {
            id: editor
            objectName: "shapeTextEditor"
            visible: viewport.showHost.canvas.editingShapeId.length > 0
            z: 2
            x: viewport.showHost.canvas.editorRect.x
            y: viewport.showHost.canvas.editorRect.y
            width: viewport.showHost.canvas.editorRect.width
            height: Math.max(viewport.showHost.canvas.editorRect.height, implicitHeight)
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            selectionColor: "#2f6fd6"
            selectedTextColor: "white"
            onVisibleChanged: if (visible) forceActiveFocus(Qt.OtherFocusReason)
            Keys.onEscapePressed: function(event) { viewport.showHost.canvas.commitTextEdit(false); event.accepted = true }
        }
    }
    MenuFromNode {
        id: contextMenu
        objectName: "showCanvasContextMenu"
        actions: viewport.showHost.actions
        node: viewport.showHost.contextMenuNode()
    }
    Connections {
        target: viewport.showHost
        function onContextMenuRequested(x, y) { contextMenu.popup(viewport.showHost.canvas.mapToItem(viewport, x, y)) }
    }
}
