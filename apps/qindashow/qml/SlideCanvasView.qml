// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Show

// The slide inside a page-fitting viewport: the host's SlideCanvasItem is
// seated in `container` (slide points; the viewport's zoom is the only
// scale, D-009), the in-place editor is a TextEdit over the shape — or the
// table cell (M67) — being edited, showing its own QTextDocument (runs and
// all), and a right press pops the arrange / group / clipboard context menu.
// Playback ▸ Play (M75, D-084) plays the selected video or audio in place
// through media/MediaSurface.qml, loaded only while it plays.
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
            // M67: in a table cell Tab / Shift+Tab move to the next / previous cell.
            Keys.onTabPressed: function(event) { event.accepted = viewport.showHost.canvas.editingCell && viewport.showHost.canvas.moveCellEdit(1) }
            Keys.onBacktabPressed: function(event) { event.accepted = viewport.showHost.canvas.editingCell && viewport.showHost.canvas.moveCellEdit(-1) }
        }
        Loader {
            id: mediaPreview
            objectName: "editorMediaPreview"
            readonly property var medium: viewport.showHost.media.selected
            z: 3
            active: viewport.showHost.media.playing && viewport.showHost.media.available && medium.url !== undefined
            x: medium.x !== undefined ? medium.x : 0
            y: medium.y !== undefined ? medium.y : 0
            width: medium.width !== undefined ? medium.width : 0
            height: medium.height !== undefined ? medium.height : 0
            source: viewport.showHost.media.surface
            onLoaded: {
                item.source = medium.url
                item.video = medium.kind === "video"
                item.loop = medium.loop
                item.volume = medium.volume / 100
                item.playing = true
            }
        }
        Connections {
            target: mediaPreview.item
            ignoreUnknownSignals: true
            function onFinished() { viewport.showHost.media.playing = false }
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
