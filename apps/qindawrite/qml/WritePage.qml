// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Write

// The pages inside a zoomable viewport: the PageTextItem paints paper and
// text in document units (zoom is the viewport's transform, D-009), the
// caret's movements scroll the viewport, and a right press pops the
// context menu the host builds (spelling first).
Tk.Viewport {
    id: viewport
    required property WriteHost write
    readonly property PageTextItem pageItem: page
    objectName: "writeViewport"
    fitMode: "none"
    contentPadding: Tk.Theme.space.md
    focus: true
    PageTextItem {
        id: page
        objectName: "writePage"
        viewZoom: viewport.zoom
        focus: true
        Component.onCompleted: viewport.write.attachPage(page)
        onEnsureVisibleRequested: function(rect) { viewport.ensureVisible(rect) }
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
