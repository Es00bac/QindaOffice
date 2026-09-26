// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Left-button gestures on the drawing (D-050). Middle drag, the wheel and
// touch navigation stay with the CanvasView underneath (this area takes the
// left button only and has no wheel handler).
//  * Pointer tool: a press on an item selects it (Shift/Ctrl toggle, groups
//    come whole) and a drag moves the selection through SpatialTransform,
//    so the move snaps to shapes, guides and the grid and the page view
//    previews it live; a press on empty paper starts ToolController's
//    marquee; the ends of a lone selected connector drag to re-glue; with
//    the format painter armed a click paints the look.
//  * Connector tool: press on a shape (or anywhere) and drag to a shape —
//    ends glue to the nearest connection point, else to the outline.
//  * Text tool: a click edits a shape's text, or drops a text box.
// Keys (forwarded from the canvas): Delete, arrows nudge (Shift = 1 unit),
// Escape, F2/Enter edit text.
MouseArea {
    id: input
    objectName: "diagramCanvasInput"

    required property var canvas
    required property var diagram
    required property var adapter
    required property var tools
    required property var spatialTransform
    required property var pageView
    property bool editing: false

    property string mode: ""
    property point startDoc
    property point startView
    property bool moved: false
    property var from: ({})
    property string endConnector: ""
    property bool endStart: false
    readonly property real glueTolerance: 10 / Math.max(0.05, canvas.zoom)

    signal textEditRequested(string id)

    anchors.fill: parent
    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    enabled: !editing
    cursorShape: diagram.tool === "connector" ? Qt.CrossCursor
               : diagram.tool === "text" ? Qt.IBeamCursor
               : diagram.formatPainter ? Qt.PointingHandCursor : Qt.ArrowCursor

    function docAt(x, y) { return canvas.mapToDocument(Qt.point(x, y)) }
    function itemAt(x, y) {
        const hits = canvas.hitTest(x, y)
        return hits.length > 0 ? adapter.itemId(hits[0].objectId) : ""
    }
    function clearFeedback() {
        pageView.pendingVisible = false
        pageView.glueVisible = false
        pageView.hoverShapeId = ""
        pageView.endDragConnector = ""
    }
    function showGlue(glue) {
        pageView.glueVisible = glue.shapeId !== "" && glue.port >= 0
        pageView.gluePoint = Qt.point(glue.x, glue.y)
        pageView.hoverShapeId = glue.shapeId
    }
    function toggle(id) {
        const next = diagram.selection.slice()
        const at = next.indexOf(id)
        if (at >= 0) next.splice(at, 1)
        else next.push(id)
        diagram.selection = next
    }
    function nearConnectorEnd(x, y) {
        if (diagram.selectionKind !== "connector" || diagram.selection.length !== 1) return -1
        const ends = pageView.connectorEnds(diagram.selection[0])
        for (let i = 0; i < 2 && ends.length >= 2; ++i) {
            const p = canvas.mapFromDocument(ends[i])
            if (Math.hypot(p.x - x, p.y - y) <= Tk.Theme.size.handle * 0.75) return i
        }
        return -1
    }

    onPressed: function(mouse) {
        canvas.forceActiveFocus()
        const d = docAt(mouse.x, mouse.y)
        startDoc = d
        startView = Qt.point(mouse.x, mouse.y)
        moved = false
        mode = ""
        if (diagram.tool === "connector") {
            from = diagram.glueAt(d.x, d.y, glueTolerance, "")
            mode = "connect"
            pageView.pendingFrom = Qt.point(from.x, from.y)
            pageView.pendingTo = pageView.pendingFrom
            showGlue(from)
            return
        }
        if (diagram.tool === "text") {
            mode = "text"
            return
        }
        const end = nearConnectorEnd(mouse.x, mouse.y)
        if (end >= 0) {
            mode = "end"
            endConnector = diagram.selection[0]
            endStart = end === 0
            pageView.endDragStart = endStart
            pageView.endDragPoint = d
            pageView.endDragConnector = endConnector
            return
        }
        const id = itemAt(mouse.x, mouse.y)
        if (diagram.formatPainter) {
            if (id.length > 0) diagram.applyFormatTo([id])
            else diagram.formatPainter = false
            return
        }
        if (id.length > 0) {
            if (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) toggle(id)
            else if (diagram.selection.indexOf(id) < 0) diagram.selection = [id]
            mode = diagram.selection.indexOf(id) >= 0 ? "move" : ""
        } else {
            mode = "marquee"
            if (!(mouse.modifiers & Qt.ShiftModifier)) diagram.selection = []
            tools.begin(canvas.viewId, d.x, d.y, 0, mouse.modifiers)
        }
    }
    onPositionChanged: function(mouse) {
        const d = docAt(mouse.x, mouse.y)
        if (!pressed) {
            pageView.hoverShapeId = diagram.tool === "connector" ? diagram.shapeAt(d.x, d.y) : ""
            return
        }
        if (mode === "move") {
            if (!moved && Math.hypot(mouse.x - startView.x, mouse.y - startView.y) < 3) return
            moved = true
            spatialTransform.update("move", startDoc, d, mouse.modifiers)
        } else if (mode === "marquee") {
            tools.update(canvas.viewId, d.x, d.y)
        } else if (mode === "connect") {
            const glue = diagram.glueAt(d.x, d.y, glueTolerance, from.shapeId || "")
            pageView.pendingTo = Qt.point(glue.x, glue.y)
            pageView.pendingVisible = true
            showGlue(glue)
        } else if (mode === "end") {
            const glue = diagram.glueAt(d.x, d.y, glueTolerance, "")
            pageView.endDragPoint = glue.port >= 0 ? Qt.point(glue.x, glue.y) : d
            showGlue(glue)
        }
    }
    onReleased: function(mouse) {
        const d = docAt(mouse.x, mouse.y)
        if (mode === "move" && moved) {
            spatialTransform.finish("move")
        } else if (mode === "marquee") {
            tools.end(canvas.viewId, d.x, d.y)
        } else if (mode === "connect") {
            const to = diagram.glueAt(d.x, d.y, glueTolerance, from.shapeId || "")
            diagram.connectItems(from, to)
        } else if (mode === "end") {
            diagram.setConnectorEnd(endConnector, endStart, d.x, d.y)
        } else if (mode === "text") {
            const id = itemAt(mouse.x, mouse.y)
            if (id.length > 0) {
                diagram.selection = [id]
                textEditRequested(id)
            } else {
                const created = diagram.addShape("text", d.x, d.y)
                if (created.length > 0) textEditRequested(created)
            }
        }
        mode = ""
        clearFeedback()
    }
    function cancelGesture() {
        if (mode === "move") spatialTransform.cancel("move")
        else if (mode === "marquee") tools.cancel()
        mode = ""
        clearFeedback()
    }
    onCanceled: cancelGesture()
    onDoubleClicked: function(mouse) {
        const id = itemAt(mouse.x, mouse.y)
        if (id.length > 0 && diagram.tool !== "connector") {
            diagram.selection = [id]
            textEditRequested(id)
        }
    }

    function handleKey(event) {
        if (editing) return
        const selected = diagram.selection.length > 0
        const step = (event.modifiers & Qt.ShiftModifier) ? 1 : Math.max(1, diagram.page.gridSpacing / 4)
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && selected) {
            diagram.deleteSelection()
        } else if (event.key === Qt.Key_Left && selected) {
            diagram.nudgeSelection(-step, 0)
        } else if (event.key === Qt.Key_Right && selected) {
            diagram.nudgeSelection(step, 0)
        } else if (event.key === Qt.Key_Up && selected) {
            diagram.nudgeSelection(0, -step)
        } else if (event.key === Qt.Key_Down && selected) {
            diagram.nudgeSelection(0, step)
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && selected) {
            textEditRequested(diagram.selection[0])
        } else if (event.key === Qt.Key_Escape) {
            if (mode.length > 0) cancelGesture()
            else if (diagram.formatPainter) diagram.formatPainter = false
            else if (diagram.tool !== "pointer") diagram.tool = "pointer"
            else if (selected) diagram.selection = []
            else return
        } else {
            return
        }
        event.accepted = true
    }
}
