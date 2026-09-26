// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Diagram.App

// The QindaDiagram window (D-050): one drawing per window. Shapes panel ·
// page (rulers, canvas, page tabs) · inspector, under the ribbon. Below
// 900 px the two side panels become drawers opened from the canvas's top
// corners, so the page keeps the width. The window answers the host's
// requests (colours, files, page setup, find, zoom) with dialogs.
//
// AGENT-GUARD (Tk.AppWindow): exactly ONE Item child — the content Item —
// with the toast inside it; dialogs are window children (not Items).
OfficeWindow {
    id: window

    readonly property DiagramHost diagramHost: host as DiagramHost
    readonly property DiagramService diagram: diagramHost !== null ? diagramHost.diagram : null
    readonly property bool narrow: width < 900
    property string drawer: ""
    readonly property real shapesWidth: Tk.Theme.size.panelMinWidth * 1.3
    readonly property real inspectorWidth: Tk.Theme.size.panelMinWidth * 1.45

    width: 1320
    height: 840

    function trigger(id) { window.host.actions.trigger(id) }

    readonly property DiagramRibbonLayout ribbonLayout: DiagramRibbonLayout {
        fontSlot: fontFaceSlot
        sizeSlot: fontSizeSlot
        themeIds: window.diagram !== null ? window.diagram.themes.map(function(t) { return "diagramTheme_" + t.id }) : []
    }
    ribbonTabs: ribbonLayout.tabs

    Component {
        id: fontFaceSlot
        Tk.ComboBox {
            readonly property var faces: ["Inter", "IBM Plex Sans", "Lato", "DejaVu Sans", "DejaVu Serif", "Liberation Mono"]
            readonly property string face: window.diagram !== null ? (window.diagram.selected.textStyle || {}).family || "" : ""
            implicitWidth: Tk.Theme.size.fieldWidth
            small: true
            model: faces
            currentIndex: Math.max(0, faces.indexOf(face || "Inter"))
            enabled: window.diagram !== null && window.diagram.selection.length > 0
            tooltip: qsTr("Font")
            onActivated: function(index) { window.diagram.setTextStyle({ family: faces[index] }) }
        }
    }
    Component {
        id: fontSizeSlot
        Tk.NumberField {
            implicitWidth: Tk.Theme.size.fieldWidth * 0.6
            value: window.diagram !== null ? (window.diagram.selected.textStyle || {}).size || 11 : 11
            from: 4
            to: 200
            stepSize: 1
            decimals: 1
            enabled: window.diagram !== null && window.diagram.selection.length > 0
            tooltip: qsTr("Font size (points)")
            onValueModified: function(value) { window.diagram.setTextStyle({ size: value }) }
        }
    }

    // ---- palette drag onto the page ----
    function dragMoved(stencilId, scenePoint) {
        const at = canvasView.pagePointAt(scenePoint)
        canvasView.pageView.dropStencil = at !== null ? stencilId : ""
        if (at !== null) canvasView.pageView.dropPoint = at
    }
    function dragFinished(stencilId, scenePoint) {
        canvasView.pageView.dropStencil = ""
        const at = canvasView.pagePointAt(scenePoint)
        if (at !== null) {
            window.diagram.addShape(stencilId, at.x, at.y)
            if (window.narrow) window.drawer = ""
        }
    }

    Item {
        objectName: "diagramContent"

        Tk.Flex {
            objectName: "diagramRoot"
            anchors.fill: parent
            direction: Tk.Flex.Row
            gap: 0
            Item {
                id: shapesSlot
                objectName: "diagramShapesSlot"
                visible: !window.narrow && window.diagramHost.shapesPanelVisible
                Tk.Flex.basis: window.shapesWidth
                Tk.Flex.shrink: 0
            }
            Tk.Divider { vertical: true; visible: shapesSlot.visible }
            DiagramCanvasView {
                id: canvasView
                host: window.diagramHost
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Divider { vertical: true; visible: inspectorSlot.visible }
            Item {
                id: inspectorSlot
                objectName: "diagramInspectorSlot"
                visible: !window.narrow && window.diagramHost.inspectorVisible
                Tk.Flex.basis: window.inspectorWidth
                Tk.Flex.shrink: 0
            }
        }

        // Narrow windows: the panels slide over the page as drawers.
        Item {
            id: shapesDrawer
            objectName: "diagramShapesDrawer"
            visible: window.narrow && window.drawer === "shapes"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(window.shapesWidth, parent.width * 0.8)
            z: 5
        }
        Item {
            id: inspectorDrawer
            objectName: "diagramInspectorDrawer"
            visible: window.narrow && window.drawer === "inspector"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(window.inspectorWidth, parent.width * 0.85)
            z: 5
        }
        Tk.Flex {
            objectName: "diagramDrawerButtons"
            visible: window.narrow
            z: 6
            anchors.top: parent.top
            anchors.right: parent.right
            // Below the horizontal ruler, inside the canvas's corner.
            anchors.topMargin: Tk.Theme.size.row + Tk.Theme.space.sm
            anchors.rightMargin: Tk.Theme.space.sm
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.IconButton {
                objectName: "diagramShapesDrawerButton"
                iconName: "shapes"
                ghost: false
                checkable: true
                checked: window.drawer === "shapes"
                tooltip: qsTr("Shapes")
                onClicked: window.drawer = window.drawer === "shapes" ? "" : "shapes"
            }
            Tk.IconButton {
                objectName: "diagramInspectorDrawerButton"
                iconName: "sliders-horizontal"
                ghost: false
                checkable: true
                checked: window.drawer === "inspector"
                tooltip: qsTr("Inspector")
                onClicked: window.drawer = window.drawer === "inspector" ? "" : "inspector"
            }
        }

        ShapesPanel {
            parent: window.narrow ? shapesDrawer : shapesSlot
            anchors.fill: parent
            diagram: window.diagram
            onShapeRequested: function(id) {
                window.diagram.addShape(id, window.diagramHost.viewCenter.x, window.diagramHost.viewCenter.y)
                if (window.narrow) window.drawer = ""
            }
            onDragMoved: function(id, scenePoint) { window.dragMoved(id, scenePoint) }
            onDragFinished: function(id, scenePoint) { window.dragFinished(id, scenePoint) }
            onDragCanceled: canvasView.pageView.dropStencil = ""
        }
        InspectorPanel {
            id: inspector
            parent: window.narrow ? inspectorDrawer : inspectorSlot
            anchors.fill: parent
            diagram: window.diagram
            spatialTransform: canvasView.spatialTransform
        }

        Tk.Toast {
            id: toast
            objectName: "diagramToast"
            z: 10
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Tk.Theme.space.lg
        }
    }

    Binding {
        target: window.host
        property: "statusInfo"
        value: window.diagram !== null
               ? qsTr("%1 · %2%").arg(window.diagram.statusText).arg(Math.round(canvasView.canvas.zoom * 100)) : ""
    }

    // ---- dialogs ----
    PageSetupDialog { id: pageSetup; diagram: window.diagram }
    ColorDialog { id: colorDialog; diagram: window.diagram }
    ReplaceDialog { id: replaceDialog; diagram: window.diagram }
    Tk.PromptDialog {
        id: renamePage
        objectName: "diagramRenamePage"
        title: qsTr("Rename page")
        label: qsTr("Page name")
        primaryText: qsTr("Rename")
        onAccepted: window.diagram.renamePage("", text)
    }
    Tk.PromptDialog {
        id: findPrompt
        objectName: "diagramFind"
        title: qsTr("Find")
        label: qsTr("Find shapes whose text or data contains")
        primaryText: qsTr("Find")
        onAccepted: window.diagram.find(text)
    }
    Dialogs.FileDialog {
        id: openFile
        objectName: "diagramOpenFile"
        property string what: "picture"
        title: what === "picture" ? qsTr("Insert picture") : what === "orgChart" ? qsTr("Org chart from CSV")
                                                                                 : qsTr("Import draw.io diagram")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: what === "picture" ? [qsTr("Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)")]
                   : what === "orgChart" ? [qsTr("CSV (*.csv *.txt)")] : [qsTr("draw.io (*.drawio *.xml)")]
        onAccepted: {
            if (what === "picture") window.diagram.insertPicture(selectedFile, window.diagramHost.viewCenter.x,
                                                                 window.diagramHost.viewCenter.y)
            else if (what === "orgChart") window.diagram.importOrgChart(selectedFile)
            else window.diagram.importDrawio(selectedFile)
        }
    }
    Dialogs.FileDialog {
        id: exportFile
        objectName: "diagramExportFile"
        property string what: "png"
        title: qsTr("Export")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: what === "pdf" ? [qsTr("PDF (*.pdf)")] : what === "svg" ? [qsTr("SVG (*.svg)")] : [qsTr("PNG (*.png)")]
        defaultSuffix: what
        onAccepted: {
            const selectionOnly = window.diagram.selection.length > 0
            if (what === "pdf") window.diagramHost.exportPdfTo(selectedFile)
            else if (what === "svg") window.diagram.exportSvg(selectedFile, selectionOnly)
            else window.diagram.exportPng(selectedFile, selectionOnly)
        }
    }

    Connections {
        target: window.diagramHost
        function onColorRequested(what) { colorDialog.openFor(what) }
        function onOpenFileRequested(what) { openFile.what = what; openFile.open() }
        function onExportRequested(what) { exportFile.what = what; exportFile.open() }
        function onPageSetupRequested() { pageSetup.openFor() }
        function onRenamePageRequested() { renamePage.text = window.diagram.page.name || ""; renamePage.open() }
        function onFindRequested() { findPrompt.open() }
        function onReplaceRequested() { replaceDialog.openWith("") }
        function onZoomRequested(how) { canvasView.zoom(how) }
        function onInspectorRequested(section) {
            inspector.show(section)
            if (window.narrow) window.drawer = "inspector"
        }
    }
    Connections {
        target: window.diagram
        function onNotify(title, text, variant) {
            toast.title = title
            toast.message = text
            toast.variant = variant
            toast.show()
        }
    }
}
