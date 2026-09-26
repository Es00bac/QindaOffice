// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaDiagram's ribbon (D-041, D-050): File · Home · Insert · Design ·
// Arrange · View, plus the contextual Shape and Connector tabs while those
// are selected. Every string is an ActionEntry id DiagramHost registers
// (AGENTS.md rule 5); the two slots are host widgets for the font face and
// size, which have no single command.
QtObject {
    property Component fontSlot: null
    property Component sizeSlot: null
    property var themeIds: []

    readonly property var alignIds: ["diagramAlignLeftAction", "diagramAlignCenterAction", "diagramAlignRightAction", "|",
                                     "diagramAlignTopAction", "diagramAlignMiddleAction", "diagramAlignBottomAction", "|",
                                     "diagramDistributeHorizontalAction", "diagramDistributeVerticalAction"]
    readonly property var orderIds: ["diagramBringToFrontAction", "diagramBringForwardAction",
                                     "diagramSendBackwardAction", "diagramSendToBackAction"]
    readonly property var weightIds: ["diagramLineWeight1Action", "diagramLineWeight2Action",
                                      "diagramLineWeight3Action", "diagramLineWeight4Action"]
    readonly property var patternIds: ["diagramLineSolidAction", "diagramLineDashAction", "diagramLineDotAction"]
    readonly property var quickStyleIds: ["diagramQuickStyle1Action", "diagramQuickStyle2Action", "diagramQuickStyle3Action",
                                          "diagramQuickStyle4Action", "diagramQuickStyle5Action", "diagramQuickStyle6Action"]
    readonly property var paperIds: ["diagramPaperLetterAction", "diagramPaperLegalAction", "diagramPaperTabloidAction", "|",
                                     "diagramPaperA5Action", "diagramPaperA4Action", "diagramPaperA3Action"]
    readonly property var layoutIds: ["diagramLayoutTopDownAction", "diagramLayoutLeftRightAction", "diagramLayoutTreeAction"]

    function lineGroup(id) {
        return { id: id, label: qsTr("Line"), items: [
            { action: "diagramLineColorAction", size: "large" },
            { id: id + "Weight", label: qsTr("Weight"), iconName: "minus", menu: weightIds },
            { id: id + "Dashes", label: qsTr("Dashes"), iconName: "square-dashed", menu: patternIds }] }
    }
    function fontGroup(id) {
        return { id: id, label: qsTr("Font"), rows: [
            [{ id: id + "Face", component: fontSlot }, { id: id + "Size", component: sizeSlot }],
            [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" },
             { action: "formatUnderlineAction", size: "icon" }, { action: "formatGrowFontAction", size: "icon" },
             { action: "formatShrinkFontAction", size: "icon" }, { action: "formatTextColorAction", size: "icon" }]] }
    }
    function paragraphGroup(id) {
        return { id: id, label: qsTr("Paragraph"), rows: [
            [{ action: "formatAlignLeftAction", size: "icon" }, { action: "formatAlignCenterAction", size: "icon" },
             { action: "formatAlignRightAction", size: "icon" }],
            [{ action: "diagramTextTopAction", size: "icon" }, { action: "diagramTextMiddleAction", size: "icon" },
             { action: "diagramTextBottomAction", size: "icon" }]] }
    }

    readonly property var tabs: [
        StandardRibbon.fileTab([
            { id: "diagramImport", label: qsTr("Import"), items: [
                { action: "diagramImportDrawioAction", size: "large" }, "diagramOrgChartFromCsvAction"] },
            { id: "diagramFilePage", label: qsTr("Page"), items: [{ action: "diagramPageSetupAction", size: "large" }] }],
            [], ["diagramExportPdfAction", "diagramExportPngAction", "diagramExportSvgAction", "diagramCopyAsImageAction"]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            { id: "clipboard", label: qsTr("Clipboard"), items: [
                { action: "editPasteAction", size: "large" }, "editCutAction", "editCopyAction", "diagramFormatPainterAction"] },
            fontGroup("homeFont"),
            paragraphGroup("homeParagraph"),
            { id: "tools", label: qsTr("Tools"), items: [
                { action: "diagramToolPointerAction", size: "large" }, "diagramToolConnectorAction", "diagramToolTextAction"] },
            { id: "shapeStyles", label: qsTr("Shape styles"), items: [
                { id: "homeQuickStyles", label: qsTr("Quick styles"), iconName: "palette", size: "large", menu: quickStyleIds },
                "diagramFillColorAction", "diagramLineColorAction", "diagramShadowAction"] },
            { id: "homeArrange", label: qsTr("Arrange"), items: [
                { id: "homeAlign", label: qsTr("Align"), iconName: "align-start-vertical", menu: alignIds },
                { id: "homeOrder", label: qsTr("Order"), iconName: "layers", menu: orderIds },
                "diagramGroupAction"] },
            StandardRibbon.editingGroup(["diagramDuplicateAction", "diagramDeleteAction"])
        ] },
        { id: "insert", label: qsTr("Insert"), keyTip: "N", groups: [
            { id: "pages", label: qsTr("Pages"), items: [
                { action: "diagramNewPageAction", size: "large" }, "diagramDuplicatePageAction",
                "diagramRenamePageAction", "diagramDeletePageAction"] },
            { id: "illustrations", label: qsTr("Illustrations"), items: [{ action: "diagramInsertPictureAction", size: "large" }] },
            { id: "diagramParts", label: qsTr("Diagram parts"), items: [
                { action: "diagramInsertContainerAction", size: "large" }, "diagramInsertSwimlaneAction",
                "diagramInsertVerticalLaneAction", "diagramInsertPhaseAction"] },
            { id: "insertText", label: qsTr("Text"), items: [
                { action: "diagramInsertTextBoxAction", size: "large" }, "diagramInsertCalloutAction", "diagramInsertNoteAction"] },
            { id: "insertOrg", label: qsTr("Org chart"), items: [{ action: "diagramOrgChartFromCsvAction", size: "large" }] }
        ] },
        { id: "design", label: qsTr("Design"), keyTip: "G", groups: [
            { id: "pageSetup", label: qsTr("Page setup"), items: [
                { action: "diagramPageSetupAction", size: "large" },
                { id: "orientation", label: qsTr("Orientation"), iconName: "file", menu: ["diagramPortraitAction", "diagramLandscapeAction"] },
                { id: "paperSize", label: qsTr("Size"), iconName: "layout-template", menu: paperIds },
                "diagramFitPageAction"] },
            { id: "themes", label: qsTr("Themes"), items: [
                { id: "themeMenu", label: qsTr("Themes"), iconName: "palette", size: "large", menu: themeIds }] },
            { id: "backgrounds", label: qsTr("Backgrounds"), items: [
                { action: "diagramBackgroundColorAction", size: "large" }, "diagramShowGridAction"] },
            { id: "layouts", label: qsTr("Layouts"), items: [
                { id: "relayout", label: qsTr("Re-layout"), iconName: "workflow", size: "large", menu: layoutIds },
                "diagramLineJumpsAction"] }
        ] },
        { id: "arrange", label: qsTr("Arrange"), keyTip: "A", groups: [
            { id: "align", label: qsTr("Align"), rows: [
                [{ action: "diagramAlignLeftAction", size: "icon" }, { action: "diagramAlignCenterAction", size: "icon" },
                 { action: "diagramAlignRightAction", size: "icon" }],
                [{ action: "diagramAlignTopAction", size: "icon" }, { action: "diagramAlignMiddleAction", size: "icon" },
                 { action: "diagramAlignBottomAction", size: "icon" }]] },
            { id: "distribute", label: qsTr("Distribute"), items: [
                "diagramDistributeHorizontalAction", "diagramDistributeVerticalAction"] },
            { id: "order", label: qsTr("Order"), items: [
                { action: "diagramBringToFrontAction", size: "large" }, "diagramBringForwardAction",
                "diagramSendBackwardAction", "diagramSendToBackAction"] },
            { id: "grouping", label: qsTr("Group"), items: [{ action: "diagramGroupAction", size: "large" }, "diagramUngroupAction"] },
            { id: "rotate", label: qsTr("Rotate & flip"), items: [
                "diagramRotateRightAction", "diagramRotateLeftAction", "diagramFlipHorizontalAction",
                "diagramFlipVerticalAction", "diagramLockAspectAction"] },
            { id: "autoLayout", label: qsTr("Auto-layout"), items: [
                { action: "diagramLayoutTopDownAction", size: "large" }, "diagramLayoutLeftRightAction", "diagramLayoutTreeAction"] }
        ] },
        StandardRibbon.viewTab([
            { id: "panels", label: qsTr("Panels"), items: [
                { action: "diagramShowShapesPanelAction", size: "large" }, "diagramShowInspectorAction", "diagramShowMinimapAction"] },
            { id: "visualAids", label: qsTr("Visual aids"), items: [
                "diagramShowRulersAction", "diagramShowGridAction", "diagramSnapAction"] },
            { id: "fit", label: qsTr("Fit"), items: [
                { action: "diagramZoomFitPageAction", size: "large" }, "diagramZoomSelectionAction"] }
        ]),
        { id: "shape", label: qsTr("Shape"), keyTip: "P", contextId: "shape", groups: [
            { id: "shapeStyle", label: qsTr("Style"), items: [
                { action: "diagramFillColorAction", size: "large" },
                { id: "shapeQuickStyles", label: qsTr("Quick styles"), iconName: "palette", menu: quickStyleIds },
                "diagramShadowAction", "diagramRoundCornersAction"] },
            lineGroup("shapeLine"),
            fontGroup("shapeFont"),
            { id: "shapeArrange", label: qsTr("Arrange"), items: [
                "diagramRotateRightAction", "diagramFlipHorizontalAction", "diagramLockAspectAction",
                { id: "shapeOrder", label: qsTr("Order"), iconName: "layers", menu: orderIds }] },
            { id: "shapeEdit", label: qsTr("Shape"), items: [
                { action: "diagramEditTextAction", size: "large" }, "diagramShapeDataAction", "diagramDuplicateAction", "diagramDeleteAction"] }
        ] },
        { id: "connector", label: qsTr("Connector"), keyTip: "C", contextId: "connector", groups: [
            { id: "routing", label: qsTr("Routing"), items: [
                { action: "diagramRouteOrthogonalAction", size: "large" }, "diagramRouteStraightAction", "diagramRouteCurvedAction"] },
            { id: "arrows", label: qsTr("Arrows"), items: [
                { action: "diagramArrowEndAction", size: "large" }, "diagramArrowStartAction", "diagramArrowBothAction",
                "diagramArrowNoneAction"] },
            lineGroup("connectorLine"),
            { id: "connectorLabel", label: qsTr("Label"), items: [
                { action: "diagramEditTextAction", size: "large" }, "diagramLineJumpsAction"] }
        ] }
    ]
}
