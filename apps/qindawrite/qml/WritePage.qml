// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Write

// The pages inside a zoomable viewport: the PageTextItem paints paper and
// text in document units (zoom is the viewport's transform, D-009), the
// caret's movements scroll the viewport, a right press pops the context
// menu the host builds (spelling first), and a click on a Word form field
// opens its list or calendar.
Tk.Viewport {
    id: viewport
    required property WriteHost write
    readonly property PageTextItem pageItem: page
    objectName: "writeViewport"
    fitMode: "none"
    contentPadding: Tk.Theme.space.md
    // One painted FBO cannot span a long document: keep the visible tile
    // within the GPU limit even at the viewport's lowest usable zoom.
    minZoom: Math.max(0.1, (viewport.viewportHeight + viewport.contentPadding * 2)
                      / Math.max(1, page.maxPaintHeight))
    onMinZoomChanged: if (zoom < minZoom) setZoom(minZoom)
    focus: true
    Item {
        id: surface
        objectName: "writeDocumentSurface"
        implicitWidth: page.implicitWidth
        implicitHeight: page.implicitHeight
        readonly property real tileHeight: Math.min(page.implicitHeight, page.maxPaintHeight)
        readonly property real visibleTop: Math.max(0, (viewport.scrollItem.contentY
                                                          - viewport.contentOriginY()) / viewport.zoom)
        readonly property real tileTop: Math.max(0, Math.min(page.implicitHeight - tileHeight,
            Math.max(visibleTop + viewport.viewportHeight / viewport.zoom - tileHeight,
                     Math.floor(Math.max(0, visibleTop - viewport.contentPadding / viewport.zoom) / 128) * 128)))
        PageTextItem {
            id: page
            objectName: "writePage"
            width: surface.width
            height: surface.tileHeight
            y: surface.tileTop
            onYChanged: update()
            viewZoom: viewport.zoom
            focus: true
            Component.onCompleted: viewport.write.attachPage(page)
            onEnsureVisibleRequested: function(rect) { viewport.ensureVisible(rect) }
            // A Word form field's list or calendar, at the click (D-080).
            FormFieldPopup { objectName: "formFieldPopup"; controls: viewport.write.controls }
        }
    }
    MenuFromNode {
        id: contextMenu
        objectName: "writePageContextMenu"
        actions: viewport.write.actions
        // The node is static (the spelling entries toggle their own
        // visibility); the host refreshes them before each request.
        node: viewport.write.contextMenuNode()
    }
    Connections {
        target: viewport.write
        function onContextMenuRequested(x, y) { contextMenu.popup(page.mapToItem(viewport, x, y)) }
    }
}
