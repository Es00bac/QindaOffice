// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaShow's ribbon (D-041): File · Home · Insert · Design · Animations ·
// Slide Show · View, plus the contextual Shape tab while shapes are
// selected. Every string is an ActionEntry id (AGENTS.md rule 5); icons
// given here only fill in for entries whose menu rows carry none.
QtObject {
    property Component fillSlot: null
    property Component strokeSlot: null

    readonly property var aiIds: ["showAirewrite", "showAishorten", "showAiexpand", "showAigrammar",
                                  "showAisummarize", "showAicontinue", "showAiexplain"]
    readonly property var animationIds: ["showAnimAppear", "showAnimFade", "showAnimFlyLeft", "showAnimFlyRight",
                                         "showAnimFlyTop", "showAnimFlyBottom", "showAnimZoom"]
    readonly property var alignIds: ["showAlignLeftAction", "showAlignHCenterAction", "showAlignRightAction", "|",
                                     "showAlignTopAction", "showAlignVCenterAction", "showAlignBottomAction", "|",
                                     "showDistributeHorizontallyAction", "showDistributeVerticallyAction"]

    readonly property var tabs: [
        StandardRibbon.fileTab([], ["showTemplate_Pitch", "showTemplate_Weekly_Review"],
                               [{ action: "showExportPdfAction", iconName: "file-down" }]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            StandardRibbon.clipboardGroup(),
            { id: "slides", label: qsTr("Slides"), items: [
                { action: "slideNewAction", size: "large" }, "slideDuplicateAction", "slideDeleteAction",
                "showSlideMoveUpAction", "showSlideMoveDownAction"] },
            { id: "text", label: qsTr("Text"), items: ["formatBoldAction", "formatItalicAction"] },
            { id: "drawing", label: qsTr("Drawing"), items: [
                { action: "showInsertTextBoxAction", size: "large" },
                "showInsertRectangleAction", "showInsertEllipseAction",
                "showInsertLineAction", "showInsertArrowAction"] },
            { id: "arrangeHome", label: qsTr("Arrange"), items: [
                { id: "orderMenu", label: qsTr("Order"), iconName: "layers",
                  menu: ["showBringToFrontAction", "showBringForwardAction", "showSendBackwardAction", "showSendToBackAction"] },
                { id: "alignMenu", label: qsTr("Align"), iconName: "align-start-vertical", menu: alignIds }] },
            StandardRibbon.editingGroup()
        ] },
        { id: "insert", label: qsTr("Insert"), keyTip: "N", groups: [
            { id: "newSlide", label: qsTr("Slides"), items: [{ action: "slideNewAction", size: "large" }] },
            { id: "images", label: qsTr("Images"), items: [{ action: "showInsertImageAction", size: "large" }] },
            { id: "shapes", label: qsTr("Shapes"), items: [
                "showInsertRectangleAction", "showInsertEllipseAction",
                "showInsertLineAction", "showInsertArrowAction"] },
            { id: "textInsert", label: qsTr("Text"), items: [{ action: "showInsertTextBoxAction", size: "large" }] }
        ] },
        { id: "design", label: qsTr("Design"), keyTip: "G", groups: [
            { id: "masters", label: qsTr("Masters"), items: [
                { action: "showMastersAction", size: "large", iconName: "layout-template" },
                { action: "showApplyMasterAction", iconName: "layout-dashboard" }] },
            { id: "guides", label: qsTr("Guides"), items: [
                { action: "showSnapToGuidesAction", iconName: "magnet" }] }
        ] },
        { id: "animations", label: qsTr("Animations"), keyTip: "A", groups: [
            { id: "build", label: qsTr("Build"), items: [
                { action: "showAppearOnClickAction", size: "large", iconName: "mouse-pointer-2" },
                { id: "effects", label: qsTr("Effect"), iconName: "sparkles", menu: animationIds }] }
        ] },
        { id: "slideShow", label: qsTr("Slide Show"), keyTip: "S", groups: [
            { id: "start", label: qsTr("Start"), items: [
                { action: "slideStartShowAction", size: "large" }] },
            { id: "share", label: qsTr("Share"), items: [
                { action: "showExportPdfAction", size: "large", iconName: "file-down" }] },
            StandardRibbon.assistantGroup(aiIds.concat(["|", "showGenerateDeckAction"]))
        ] },
        StandardRibbon.viewTab([]),
        { id: "shape", label: qsTr("Shape"), keyTip: "P", contextId: "shape", groups: [
            { id: "style", label: qsTr("Style"), rows: [
                [{ id: "fill", component: fillSlot }, { id: "stroke", component: strokeSlot }],
                [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" }]] },
            { id: "order", label: qsTr("Order"), items: [
                "showBringToFrontAction", "showBringForwardAction", "showSendBackwardAction", "showSendToBackAction"] },
            { id: "align", label: qsTr("Align"), rows: [
                [{ action: "showAlignLeftAction", size: "icon" }, { action: "showAlignHCenterAction", size: "icon" },
                 { action: "showAlignRightAction", size: "icon" }, { action: "showDistributeHorizontallyAction", size: "icon" }],
                [{ action: "showAlignTopAction", size: "icon" }, { action: "showAlignVCenterAction", size: "icon" },
                 { action: "showAlignBottomAction", size: "icon" }, { action: "showDistributeVerticallyAction", size: "icon" }]] },
            { id: "grouping", label: qsTr("Group"), items: ["showGroupAction", "showUngroupAction"] },
            { id: "shapeEdit", label: qsTr("Shape"), items: ["showDuplicateShapeAction", "showDeleteShapeAction"] }
        ] }
    ]
}
