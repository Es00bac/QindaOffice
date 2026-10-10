// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The drawing surface (D-050): rulers, the page, and the editor overlays,
// built on QindaTK's canvas stack. A DiagramCanvasAdapter shows the current
// page to a transparent Tk.CanvasView as pick proxies; DiagramPageView
// paints the page underneath with the engine's painter (the same one print
// and export use). ToolController owns the selection and the marquee,
// SpatialTransform moves, resizes and rotates with SnapModel's snapping,
// SelectionOverlay draws the handles, NavigatorView is the minimap, and
// CanvasInput turns pointer gestures into diagram edits.
//
// AGENT-CONTRACT: the service's selection (item ids) and the tool
// controller's (canvas ids) are kept equal here in both directions; the
// adapter's ids are stable per item, so a rebuild keeps the selection.
Item {
    id: root
    objectName: "diagramCanvasView"

    required property DiagramHost host
    readonly property DiagramService diagram: host.diagram
    readonly property var page: diagram.page
    readonly property alias canvas: canvas
    readonly property alias pageView: pageView
    readonly property alias tools: tools
    readonly property alias spatialTransform: spatial
    readonly property alias adapter: adapter
    readonly property alias textEditor: textEditor
    readonly property bool metric: Qt.locale().measurementSystem === Locale.MetricSystem
    readonly property real rulerUnit: metric ? 96 / 2.54 : 96

    // ---- navigation (View commands) ----
    function zoom(how) {
        if (how === "in") canvas.zoomIn()
        else if (how === "out") canvas.zoomOut()
        else if (how === "reset") canvas.setZoomCentered(1)
        else if (how === "page") canvas.fitDocument()
        else if (how === "selection") {
            const r = diagram.selectionBounds()
            if (r.width > 0 && r.height > 0) canvas.fitRect(Qt.rect(r.x - 24, r.y - 24, r.width + 48, r.height + 48))
        }
    }
    function reveal(rect) {
        const visible = canvas.visibleDocumentRect
        if (rect.x < visible.x || rect.y < visible.y || rect.x + rect.width > visible.x + visible.width
                || rect.y + rect.height > visible.y + visible.height)
            canvas.viewState.setCenter(rect.x + rect.width / 2, rect.y + rect.height / 2)
    }
    // Scene (window content) point -> page point, or null outside the canvas.
    function pagePointAt(scenePoint) {
        const local = canvas.mapFromItem(null, scenePoint.x, scenePoint.y)
        if (local.x < 0 || local.y < 0 || local.x > canvas.width || local.y > canvas.height) return null
        return canvas.mapToDocument(Qt.point(local.x, local.y))
    }
    function beginTextEdit(id) { if (id.length > 0) textEditor.begin(id) }

    DiagramCanvasAdapter { id: adapter; service: root.diagram }
    Tk.ToolController { id: tools; document: adapter; activeTool: "select" }
    Tk.CanvasViewState { id: pose }
    Tk.SnapModel {
        id: snap
        document: adapter
        viewState: pose
        enabled: root.host.snapEnabled
        gridSpacing: root.host.snapEnabled && root.page.grid ? root.page.gridSpacing / 4 : 0
    }
    Tk.SpatialTransform { id: spatial; objectName: "diagramTransform"; document: adapter; tools: tools; snapModel: snap; viewState: pose }

    // The two selections stay one.
    function sameIds(a, b) {
        if (a.length !== b.length) return false
        for (let i = 0; i < a.length; ++i) if (String(a[i]) !== String(b[i])) return false
        return true
    }
    function pushSelection() {
        const ids = adapter.canvasIds(diagram.selection)
        if (!sameIds(ids, tools.selection)) tools.selection = ids
    }
    Connections {
        target: root.diagram
        function onSelectionChanged() { root.pushSelection() }
        function onRevealRequested(rect) { root.reveal(rect) }
        function onTextEditRequested(id) { root.beginTextEdit(id) }
        // Each page opens fitted, whatever the last one was zoomed to.
        function onCurrentPageChanged() { Qt.callLater(canvas.fitDocument) }
    }
    Connections {
        target: tools
        function onSelectionChanged() { root.diagram.selection = adapter.itemIds(tools.selection) }
    }
    Connections {
        target: adapter
        function onChanged() { root.pushSelection() }
    }
    Connections {
        target: canvas
        function onViewChanged() {
            const centre = canvas.mapToDocument(Qt.point(canvas.width / 2, canvas.height / 2))
            root.host.viewCenter = Qt.point(centre.x, centre.y)
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0

        Tk.Flex {
            objectName: "diagramRulerRow"
            visible: root.host.rulersVisible
            direction: Tk.Flex.Row
            gap: 0
            Tk.Box {
                objectName: "diagramRulerCorner"
                Tk.Flex.basis: Tk.Theme.size.row
                Tk.Flex.shrink: 0
                implicitHeight: Tk.Theme.size.row
                color: Tk.Theme.color.panelAlt
            }
            Tk.Ruler {
                objectName: "diagramRulerH"
                orientation: Qt.Horizontal
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                unit: root.metric ? "cm" : "in"
                pixelsPerUnit: canvas.zoom * root.rulerUnit
                origin: canvas.width / 2 - canvas.centerX * canvas.zoom
                majorEvery: 1
                minorEvery: root.metric ? 0.1 : 0.125
                cursorPosition: input.containsMouse ? input.mouseX : -1
                band: [origin, origin + root.page.width * canvas.zoom]
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: 0
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Ruler {
                objectName: "diagramRulerV"
                visible: root.host.rulersVisible
                orientation: Qt.Vertical
                Tk.Flex.shrink: 0
                unit: root.metric ? "cm" : "in"
                pixelsPerUnit: canvas.zoom * root.rulerUnit
                origin: canvas.height / 2 - canvas.centerY * canvas.zoom
                majorEvery: 1
                minorEvery: root.metric ? 0.1 : 0.125
                cursorPosition: input.containsMouse ? input.mouseY : -1
                band: [origin, origin + root.page.height * canvas.zoom]
            }
            Item {
                id: canvasHost
                objectName: "diagramCanvasHost"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                clip: true

                DiagramPageView {
                    id: pageView
                    objectName: "diagramPageView"
                    anchors.fill: parent
                    service: root.diagram
                    surface: canvas
                    showGrid: true
                    canvasColor: Tk.Theme.color.canvas
                    accentColor: Tk.Theme.color.accent
                    gridColor: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.wash * 1.5)
                    shadowColor: Tk.Theme.alpha(Tk.Theme.color.shadow, Tk.Theme.opacity.muted)
                    handleColor: Tk.Theme.color.panel
                    previewActive: spatial.active
                    previewFrom: spatial.selectionBounds
                    previewCorners: spatial.previewCorners
                    showPageBreaks: root.host.pageBreaksVisible
                }
                Tk.CanvasView {
                    id: canvas
                    objectName: "diagramCanvas"
                    anchors.fill: parent
                    // Transparent: the page view underneath paints the pixels.
                    canvasColor: "transparent"
                    document: adapter
                    toolController: tools
                    viewState: pose
                    fitMode: "page"
                    accessibleName: qsTr("Drawing page %1").arg(root.page.name || "")
                    focus: true
                    Keys.onPressed: function(event) { input.handleKey(event) }

                    CanvasInput {
                        id: input
                        canvas: canvas
                        diagram: root.diagram
                        adapter: adapter
                        tools: tools
                        spatialTransform: spatial
                        pageView: pageView
                        editing: textEditor.visible
                        autoConnect: root.host.autoConnectEnabled
                        onTextEditRequested: function(id) { root.beginTextEdit(id) }
                        onCommentRequested: function(id) {
                            root.host.commentsPaneVisible = true
                            root.diagram.activeComment = id
                        }
                    }
                    Tk.SelectionOverlay {
                        objectName: "diagramSelectionHandles"
                        anchors.fill: parent
                        surface: canvas
                        visible: tools.selection.length > 0 && !textEditor.visible
                                 && (root.diagram.selectionKind === "shape" || root.diagram.selectionKind === "mixed")
                        handlesEnabled: root.diagram.tool === "pointer"
                        handleSize: Tk.Theme.size.grip
                        selectionBounds: spatial.previewBounds
                        selectionCorners: spatial.previewCorners
                        geometryRevision: adapter.revision
                        // A shape with a locked aspect ratio resizes like Shift.
                        function modifiersFor(handle, modifiers) {
                            const corner = handle === "topLeft" || handle === "topRight"
                                        || handle === "bottomLeft" || handle === "bottomRight"
                            return corner && root.diagram.selected.aspectLocked ? (modifiers | Qt.ShiftModifier) : modifiers
                        }
                        onTransformRequested: function(handle, start, current, modifiers) {
                            spatial.update(handle, start, current, modifiersFor(handle, modifiers))
                        }
                        onTransformFinished: function(handle) { spatial.finish(handle) }
                        onTransformCanceled: function(handle) { spatial.cancel(handle) }
                        onTransformNudgeRequested: function(handle, dx, dy, modifiers) { spatial.nudge(handle, dx, dy, modifiers) }
                        onRotateStepRequested: function(degrees, modifiers) { spatial.rotateBy(degrees, modifiers) }
                    }
                    TextEditorOverlay {
                        id: textEditor
                        canvas: canvas
                        diagram: root.diagram
                    }
                }

                Tk.Card {
                    id: minimap
                    objectName: "diagramMinimap"
                    visible: root.host.minimapVisible && canvasHost.width >= Tk.Theme.size.panelMinWidth * 2.5
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Tk.Theme.space.md
                    width: Tk.Theme.size.panelMinWidth
                    height: Tk.Theme.size.panelMinWidth * 0.7
                    padding: Tk.Theme.space.xs
                    Item {
                        DiagramPageView {
                            anchors.fill: parent
                            service: root.diagram
                            surface: navigator.preview
                            overview: true
                            canvasColor: Tk.Theme.color.panelAlt
                            accentColor: Tk.Theme.color.accent
                            shadowColor: Tk.Theme.alpha(Tk.Theme.color.shadow, Tk.Theme.opacity.muted)
                        }
                        Tk.NavigatorView {
                            id: navigator
                            objectName: "diagramNavigator"
                            anchors.fill: parent
                            mainView: canvas
                            tooltip: qsTr("Page overview: drag to move the view")
                            Component.onCompleted: navigator.preview.canvasColor = "transparent"
                        }
                    }
                }
            }
        }
        PageBar {
            objectName: "diagramPageBar"
            host: root.host
            zoom: canvas.zoom
            onZoomRequested: function(how) { root.zoom(how) }
            onZoomChosen: function(value) { canvas.setZoomCentered(value) }
        }
    }
}
