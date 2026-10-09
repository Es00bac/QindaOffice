// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaShow's ribbon (D-041): File · Home · Insert · Design · Animations ·
// Slide Show · Review · View, plus the contextual Shape tab while shapes
// are selected, (M67, D-076) Table Design / Table Layout while a table is
// and Chart while a chart is, and (M75, D-084) Slide Master while the
// Slide Master view is open and Playback while a video or audio is
// selected. Every string is an ActionEntry id (AGENTS.md rule 5); icons
// given here only fill in for entries whose menu rows carry none.
QtObject {
    property Component fillSlot: null
    property Component strokeSlot: null
    property Component shadingSlot: null
    property Component penSlot: null
    // The Design tab's built-in sets (ShowDesignTab: showTheme_<Name>, …).
    property var themeIds: []
    property var colorIds: []
    property var fontIds: []
    property var effectIds: []

    readonly property var tableOptionIds: ["showTableHeaderRowAction", "showTableTotalRowAction", "showTableBandedRowsAction",
                                           "showTableFirstColumnAction", "showTableLastColumnAction", "showTableBandedColumnsAction"]
    readonly property var borderIds: ["showTableBordersAllAction", "showTableBordersOutsideAction", "showTableBordersInsideAction", "|",
                                      "showTableBordersTopAction", "showTableBordersBottomAction", "showTableBordersLeftAction",
                                      "showTableBordersRightAction", "|", "showTableBordersInsideHorizontalAction",
                                      "showTableBordersInsideVerticalAction", "|", "showTableBordersNoneAction"]

    readonly property var sectionIds: ["showAddSectionAction", "showRenameSectionAction", "showRemoveSectionAction",
                                       "showRemoveAllSectionsAction", "|", "showMoveSectionUpAction",
                                       "showMoveSectionDownAction", "|", "showCollapseSectionsAction", "showExpandSectionsAction"]
    readonly property var slideSizeIds: ["showSlideSizeWidescreenAction", "showSlideSizeStandardAction", "|",
                                         "showSlideSizeCustomAction"]
    readonly property var placeholderIds: ["showInsertContentPlaceholderAction", "showInsertTextPlaceholderAction",
                                           "showInsertPicturePlaceholderAction"]

    // Themes, Colors, Fonts, Effects: the Design tab's and the Slide Master
    // tab's Edit Theme group (`prefix` keeps their item ids apart).
    function themeMenus(prefix) {
        return [{ id: prefix + "themesMenu", label: qsTr("Themes"), iconName: "paintbrush", size: "large",
                  menu: themeIds.concat(["|", "showThemeBrowseAction"]) },
                { id: prefix + "colorsMenu", label: qsTr("Colors"), iconName: "palette", menu: colorIds },
                { id: prefix + "fontsMenu", label: qsTr("Fonts"), iconName: "type", menu: fontIds },
                { id: prefix + "effectsMenu", label: qsTr("Effects"), iconName: "sparkles", menu: effectIds }]
    }

    readonly property var aiIds: ["showAirewrite", "showAishorten", "showAiexpand", "showAigrammar",
                                  "showAisummarize", "showAicontinue", "showAiexplain"]
    readonly property var animationIds: ["showAnimAppear", "showAnimFade", "showAnimFlyLeft", "showAnimFlyRight",
                                         "showAnimFlyTop", "showAnimFlyBottom", "showAnimZoom", "showAnimWipe",
                                         "showAnimSplit", "|", "showAnimSpin", "showAnimGrowShrink"]
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
                "showSlideMoveUpAction", "showSlideMoveDownAction",
                { id: "sectionMenu", label: qsTr("Section"), iconName: "folder-plus", menu: sectionIds }] },
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
            { id: "tables", label: qsTr("Tables"), items: [{ action: "showInsertTableAction", size: "large" }] },
            { id: "images", label: qsTr("Images"), items: [{ action: "showInsertImageAction", size: "large" }] },
            { id: "shapes", label: qsTr("Shapes"), items: [
                "showInsertRectangleAction", "showInsertEllipseAction",
                "showInsertLineAction", "showInsertArrowAction"] },
            { id: "textInsert", label: qsTr("Text"), items: [{ action: "showInsertTextBoxAction", size: "large" }] },
            { id: "commentsInsert", label: qsTr("Comments"), items: [{ action: "showNewCommentAction", size: "large" }] },
            { id: "media", label: qsTr("Media"), items: [
                { action: "showInsertVideoAction", size: "large" }, { action: "showInsertAudioAction", size: "large" }] }
        ] },
        { id: "design", label: qsTr("Design"), keyTip: "G", groups: [
            { id: "themes", label: qsTr("Themes"), items: themeMenus("") },
            { id: "customize", label: qsTr("Customize"), items: [
                { id: "slideSizeMenu", label: qsTr("Slide Size"), iconName: "monitor", size: "large", menu: slideSizeIds },
                { action: "showFormatBackgroundAction", size: "large" },
                "showHideBackgroundGraphicsAction", "showBackgroundApplyToAllAction", "showBackgroundResetAction"] },
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
                { action: "slideStartShowAction", size: "large" }, { action: "showPresenterViewAction", size: "large" }] },
            { id: "share", label: qsTr("Share"), items: [
                { action: "showExportPdfAction", size: "large", iconName: "file-down" }] },
            StandardRibbon.assistantGroup(aiIds.concat(["|", "showGenerateDeckAction"]))
        ] },
        { id: "review", label: qsTr("Review"), keyTip: "R", groups: [
            { id: "comments", label: qsTr("Comments"), items: [
                { action: "showNewCommentAction", size: "large" }, "showDeleteCommentAction",
                "showPreviousCommentAction", "showNextCommentAction", { action: "showCommentsPaneAction", size: "large" }] }
        ] },
        StandardRibbon.viewTab([
            { id: "masterViews", label: qsTr("Master Views"), items: [{ action: "showSlideMasterViewAction", size: "large" }] },
            { id: "notes", label: qsTr("Notes"), items: [{ action: "showViewNotesAction", size: "large" }] }]),
        { id: "slideMaster", label: qsTr("Slide Master"), keyTip: "M", contextId: "masterView", groups: [
            { id: "editMaster", label: qsTr("Edit Master"), items: [
                { action: "showInsertLayoutAction", size: "large" }, "showDuplicateLayoutAction", "showDeleteLayoutAction",
                "showRenameLayoutAction", "showPreserveLayoutAction"] },
            { id: "masterLayout", label: qsTr("Master Layout"), items: [
                { id: "placeholderMenu", label: qsTr("Insert Placeholder"), iconName: "layout-grid", size: "large",
                  menu: placeholderIds },
                "showLayoutTitleAction", "showLayoutFootersAction"] },
            { id: "editTheme", label: qsTr("Edit Theme"), items: themeMenus("master") },
            { id: "masterBackground", label: qsTr("Background"), items: [
                { action: "showFormatBackgroundAction", id: "masterFormatBackground" },
                { action: "showHideBackgroundGraphicsAction", id: "masterHideBackgroundGraphics" },
                { action: "showMasterTextStylesAction" }] },
            { id: "masterClose", label: qsTr("Close"), items: [{ action: "showCloseMasterViewAction", size: "large" }] }
        ] },
        { id: "playback", label: qsTr("Playback"), keyTip: "JN", contextId: "media", groups: [
            { id: "preview", label: qsTr("Preview"), items: [{ action: "showMediaPlayAction", size: "large" }] },
            { id: "mediaOptions", label: qsTr("Options"), items: [
                "showMediaStartOnClickAction", "showMediaStartAutomaticallyAction",
                "showMediaLoopAction", "showMediaHideAction"] },
            { id: "poster", label: qsTr("Poster Frame"), items: [{ action: "showMediaPosterAction", size: "large" }] }
        ] },
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
        ] },
        { id: "tableDesign", label: qsTr("Table Design"), keyTip: "JT", contextId: "table", groups: [
            { id: "tableOptions", label: qsTr("Table Style Options"), items: tableOptionIds },
            { id: "tableShading", label: qsTr("Shading"), items: [
                { id: "shading", component: shadingSlot }, "showTableShadingAction"] },
            { id: "tableBorders", label: qsTr("Borders"), items: [
                { id: "pen", component: penSlot },
                { id: "bordersMenu", label: qsTr("Borders"), iconName: "grid-3x3", size: "large", menu: borderIds }] }
        ] },
        { id: "tableLayout", label: qsTr("Table Layout"), keyTip: "JL", contextId: "table", groups: [
            { id: "rowsColumns", label: qsTr("Rows & Columns"), items: [
                { action: "showTableInsertRowAboveAction", size: "large" }, "showTableInsertRowBelowAction",
                "showTableInsertColumnLeftAction", "showTableInsertColumnRightAction",
                "showTableDeleteRowsAction", "showTableDeleteColumnsAction"] },
            { id: "merge", label: qsTr("Merge"), items: [
                { action: "showTableMergeCellsAction", size: "large" }, { action: "showTableSplitCellsAction", size: "large" }] },
            { id: "cellSize", label: qsTr("Cell Size"), items: ["showTableDistributeRowsAction", "showTableDistributeColumnsAction"] }
        ] },
        { id: "chart", label: qsTr("Chart"), keyTip: "JC", contextId: "chart", groups: [
            { id: "chartData", label: qsTr("Data"), items: [{ action: "showChartEditDataAction", size: "large" }] }
        ] }
    ]
}
