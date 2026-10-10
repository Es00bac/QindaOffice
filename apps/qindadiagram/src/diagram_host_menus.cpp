// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramHost: the menu tree (the panel export and the command search read
// it) and the commands' enabled/checked state.
#include "diagram_host.h"

#include "standard_actions.h"
#include "themes.h"

namespace QindaOffice::Apps::Diagram {

using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

MenuNode a(const char *raw)
{
    return MenuNode::action(id(raw));
}

MenuNode sub(const char *menuId, const QString &title, QList<MenuNode> items)
{
    return MenuNode::submenu(id(menuId), title, std::move(items));
}

} // namespace

void DiagramHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    // File: import/export before Close Window (standard_actions.h anchor).
    const QString close = id(ActionId::kFileCloseWindow);
    tree->insertInto(QStringLiteral("fileMenu"), close,
                     sub("diagramImportMenu", tr("&Import"), {a("diagramImportDrawioAction")}));
    tree->insertInto(QStringLiteral("fileMenu"), close,
                     sub("diagramExportMenu", tr("&Export"),
                         {a("diagramExportPdfAction"), a("diagramExportPngAction"), a("diagramExportSvgAction"),
                          MenuNode::separator(), a("diagramCopyAsImageAction")}));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("diagramPageSetupAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());

    // Edit: the diagram's own edits after Select All.
    const QString find = id(ActionId::kEditFind);
    for (const MenuNode &node : {a("diagramDuplicateAction"), a("diagramDeleteAction"), MenuNode::separator(),
                                 a("diagramEditTextAction"), a("diagramShapeDataAction"), a("diagramFormatPainterAction"),
                                 sub("diagramToolsMenu", tr("T&ools"),
                                     {a("diagramToolPointerAction"), a("diagramToolTextAction"),
                                      a("diagramToolConnectorAction")}),
                                 // M87: Visio's Home ▸ Editing ▸ Layers.
                                 sub("diagramLayersMenu", tr("La&yers"),
                                     {a("diagramLayerPropertiesAction"), a("diagramAssignLayerAction")}),
                                 MenuNode::separator()}) {
        tree->insertInto(QStringLiteral("editMenu"), find, node);
    }

    // View: panels first, fit commands after the zoom trio.
    const QString zoomIn = id(ActionId::kViewZoomIn);
    for (const MenuNode &node : {a("diagramShowShapesPanelAction"), a("diagramShowInspectorAction"),
                                 a("diagramShowMinimapAction"), a("diagramShowRulersAction"), a("diagramShowGridAction"),
                                 a("diagramSnapAction"), a("diagramShowPageBreaksAction"), a("diagramAutoConnectAction"),
                                 MenuNode::separator()}) {
        tree->insertInto(QStringLiteral("viewMenu"), zoomIn, node);
    }
    const QString toolBars = id(ActionId::kViewShowToolBars);
    tree->insertInto(QStringLiteral("viewMenu"), toolBars, a("diagramZoomFitPageAction"));
    tree->insertInto(QStringLiteral("viewMenu"), toolBars, a("diagramZoomSelectionAction"));
    tree->insertInto(QStringLiteral("viewMenu"), toolBars, MenuNode::separator());

    const QString before = QStringLiteral("aiMenu");
    tree->insertMenuBefore(before, sub("diagramInsertMenu", tr("&Insert"),
                                       {a("diagramNewPageAction"), a("diagramDuplicatePageAction"),
                                        a("diagramRenamePageAction"), a("diagramDeletePageAction"),
                                        a("diagramInsertBackgroundPageAction"), MenuNode::separator(),
                                        a("diagramInsertTextBoxAction"), a("diagramInsertPictureAction"),
                                        a("diagramInsertNoteAction"), a("diagramInsertCalloutAction"), MenuNode::separator(),
                                        a("diagramInsertContainerAction"), a("diagramInsertSwimlaneAction"),
                                        a("diagramInsertSwimlaneBeforeAction"), a("diagramInsertVerticalLaneAction"),
                                        a("diagramInsertPhaseAction"), MenuNode::separator(),
                                        a(ActionId::kInsertHyperlink), a("diagramFollowLinkAction"), MenuNode::separator(),
                                        a("diagramOrgChartFromCsvAction")}));
    tree->insertMenuBefore(
        before,
        sub("diagramFormatMenu", tr("F&ormat"),
            {sub("diagramTextMenu", tr("&Text"),
                 {a(ActionId::kFormatBold), a(ActionId::kFormatItalic), a(ActionId::kFormatUnderline),
                  MenuNode::separator(), a(ActionId::kFormatGrowFont), a(ActionId::kFormatShrinkFont),
                  a(ActionId::kFormatTextColor), MenuNode::separator(), a(ActionId::kFormatAlignLeft),
                  a(ActionId::kFormatAlignCenter), a(ActionId::kFormatAlignRight), MenuNode::separator(),
                  a("diagramTextTopAction"), a("diagramTextMiddleAction"), a("diagramTextBottomAction")}),
             MenuNode::separator(), a("diagramFillColorAction"), a("diagramLineColorAction"),
             sub("diagramLineWeightMenu", tr("Line &Weight"),
                 {a("diagramLineWeight1Action"), a("diagramLineWeight2Action"), a("diagramLineWeight3Action"),
                  a("diagramLineWeight4Action")}),
             sub("diagramLinePatternMenu", tr("Line &Pattern"),
                 {a("diagramLineSolidAction"), a("diagramLineDashAction"), a("diagramLineDotAction")}),
             a("diagramShadowAction"), a("diagramRoundCornersAction"),
             sub("diagramQuickStylesMenu", tr("&Quick Styles"),
                 {a("diagramQuickStyle1Action"), a("diagramQuickStyle2Action"), a("diagramQuickStyle3Action"),
                  a("diagramQuickStyle4Action"), a("diagramQuickStyle5Action"), a("diagramQuickStyle6Action")}),
             MenuNode::separator(),
             sub("diagramConnectorMenu", tr("&Connector"),
                 {a("diagramRouteOrthogonalAction"), a("diagramRouteStraightAction"), a("diagramRouteCurvedAction"),
                  MenuNode::separator(), a("diagramArrowNoneAction"), a("diagramArrowEndAction"),
                  a("diagramArrowStartAction"), a("diagramArrowBothAction"), MenuNode::separator(),
                  a("diagramLineJumpsAction"),
                  sub("diagramLineJumpStyleMenu", tr("Line Jump &Style"),
                      {a("diagramJumpArcAction"), a("diagramJumpGapAction"), a("diagramJumpSquareAction"),
                       MenuNode::separator(), a("diagramJumpLastRoutedAction"), a("diagramJumpHorizontalAction"),
                       a("diagramJumpVerticalAction")})})}));
    tree->insertMenuBefore(
        before,
        sub("diagramArrangeMenu", tr("A&rrange"),
            {sub("diagramAlignMenu", tr("&Align"),
                 {a("diagramAlignLeftAction"), a("diagramAlignCenterAction"), a("diagramAlignRightAction"),
                  MenuNode::separator(), a("diagramAlignTopAction"), a("diagramAlignMiddleAction"),
                  a("diagramAlignBottomAction")}),
             sub("diagramDistributeMenu", tr("&Distribute"),
                 {a("diagramDistributeHorizontalAction"), a("diagramDistributeVerticalAction")}),
             sub("diagramOrderMenu", tr("&Order"),
                 {a("diagramBringToFrontAction"), a("diagramBringForwardAction"), a("diagramSendBackwardAction"),
                  a("diagramSendToBackAction")}),
             MenuNode::separator(), a("diagramGroupAction"), a("diagramUngroupAction"), MenuNode::separator(),
             a("diagramRotateRightAction"), a("diagramRotateLeftAction"), a("diagramFlipHorizontalAction"),
             a("diagramFlipVerticalAction"), a("diagramLockAspectAction"), MenuNode::separator(),
             // M87: Visio's Container Format tab and AutoConnect.
             sub("diagramContainerMenu", tr("&Container"),
                 {a("diagramContainerFitAction"), a("diagramContainerDisbandAction"), a("diagramContainerSelectAction"),
                  MenuNode::separator(), a("diagramContainerResizeNoneAction"), a("diagramContainerResizeExpandAction"),
                  a("diagramContainerResizeFitAction")}),
             sub("diagramAutoConnectMenu", tr("AutoConnec&t"),
                 {a("diagramConnectUpAction"), a("diagramConnectRightAction"), a("diagramConnectDownAction"),
                  a("diagramConnectLeftAction")}),
             sub("diagramLayoutMenu", tr("Auto-&Layout"),
                 {a("diagramLayoutTopDownAction"), a("diagramLayoutBottomUpAction"), a("diagramLayoutLeftRightAction"),
                  a("diagramLayoutRightLeftAction"), MenuNode::separator(), a("diagramLayoutTreeAction"),
                  a("diagramLayoutCompactTreeAction"), a("diagramLayoutRadialAction"), a("diagramLayoutCircularAction"),
                  MenuNode::separator(), a("diagramLayoutOptionsAction")})}));
    QList<MenuNode> themeItems;
    for (const auto &t : ::QindaOffice::Diagram::themes()) {
        themeItems.append(MenuNode::action(QStringLiteral("diagramTheme_") + t.id));
    }
    tree->insertMenuBefore(
        before, sub("diagramDesignMenu", tr("&Design"),
                    {a("diagramPortraitAction"), a("diagramLandscapeAction"),
                     sub("diagramPaperMenu", tr("Paper &Size"),
                         {a("diagramPaperLetterAction"), a("diagramPaperLegalAction"), a("diagramPaperTabloidAction"),
                          a("diagramPaperA5Action"), a("diagramPaperA4Action"), a("diagramPaperA3Action")}),
                     a("diagramFitPageAction"), a("diagramAutoSizeAction"), a("diagramBackgroundColorAction"),
                     a("diagramBordersTitlesAction"), MenuNode::separator(),
                     sub("diagramThemesMenu", tr("&Themes"), themeItems)}));
    // M87: Visio's Data and Review tabs as menus.
    tree->insertMenuBefore(before, sub("diagramDataMenu", tr("Da&ta"),
                                       {a("diagramLinkDataAction"), a("diagramExternalDataAction"),
                                        a("diagramRefreshDataAction"), a("diagramUnlinkDataAction")}));
    tree->insertMenuBefore(before, sub("diagramReviewMenu", tr("Revie&w"),
                                       {a("diagramNewCommentAction"), a("diagramDeleteCommentAction"),
                                        MenuNode::separator(), a("diagramPreviousCommentAction"),
                                        a("diagramNextCommentAction"), MenuNode::separator(),
                                        a("diagramShowCommentsAction")}));
}

void DiagramHost::syncEnablement()
{
    const QStringList selection = m_service->selection();
    const QString kind = m_service->selectionKind();
    const bool any = !selection.isEmpty();
    const bool shapes = kind == QLatin1String("shape") || kind == QLatin1String("mixed");
    const bool connectors = kind == QLatin1String("connector") || kind == QLatin1String("mixed");
    int shapeCount = 0;
    if (const auto *page = m_service->currentPage()) {
        for (const QString &item : selection) {
            shapeCount += page->shapeIndex(item) >= 0 ? 1 : 0;
        }
    }
    const auto set = [this](std::initializer_list<const char *> ids, bool enabled) {
        for (const char *raw : ids) {
            actions()->setEnabled(id(raw), enabled);
        }
    };
    set({ActionId::kEditCut, ActionId::kEditCopy, "diagramDuplicateAction", "diagramDeleteAction", "diagramEditTextAction",
         "diagramShapeDataAction", ActionId::kFormatBold, ActionId::kFormatItalic, ActionId::kFormatUnderline,
         ActionId::kFormatGrowFont, ActionId::kFormatShrinkFont, ActionId::kFormatTextColor, ActionId::kFormatAlignLeft,
         ActionId::kFormatAlignCenter, ActionId::kFormatAlignRight, "diagramTextTopAction", "diagramTextMiddleAction",
         "diagramTextBottomAction", "diagramLineColorAction", "diagramLineWeight1Action", "diagramLineWeight2Action",
         "diagramLineWeight3Action", "diagramLineWeight4Action", "diagramLineSolidAction", "diagramLineDashAction",
         "diagramLineDotAction", "diagramQuickStyle1Action", "diagramQuickStyle2Action", "diagramQuickStyle3Action",
         "diagramQuickStyle4Action", "diagramQuickStyle5Action", "diagramQuickStyle6Action", "diagramBringToFrontAction",
         "diagramBringForwardAction", "diagramSendBackwardAction", "diagramSendToBackAction", "diagramZoomSelectionAction",
         "diagramFormatPainterAction"},
        any);
    set({"diagramFillColorAction", "diagramShadowAction", "diagramRoundCornersAction", "diagramLockAspectAction",
         "diagramRotateRightAction", "diagramRotateLeftAction", "diagramFlipHorizontalAction", "diagramFlipVerticalAction"},
        shapes);
    set({"diagramAlignLeftAction", "diagramAlignCenterAction", "diagramAlignRightAction", "diagramAlignTopAction",
         "diagramAlignMiddleAction", "diagramAlignBottomAction"},
        shapeCount >= 2);
    set({"diagramDistributeHorizontalAction", "diagramDistributeVerticalAction"}, shapeCount >= 3);
    set({"diagramGroupAction"}, selection.size() >= 2);
    bool grouped = false;
    if (const auto *page = m_service->currentPage()) {
        for (const QString &item : selection) {
            const auto *s = page->shape(item);
            const auto *c = page->connector(item);
            grouped = grouped || (s != nullptr && !s->groupId.isEmpty()) || (c != nullptr && !c->groupId.isEmpty());
        }
    }
    set({"diagramUngroupAction"}, grouped);
    set({"diagramRouteOrthogonalAction", "diagramRouteStraightAction", "diagramRouteCurvedAction",
         "diagramArrowNoneAction", "diagramArrowEndAction", "diagramArrowStartAction", "diagramArrowBothAction"},
        connectors);
    actions()->setEnabled(id(ActionId::kEditPaste), m_service->canPaste());
    actions()->setEnabled(id("diagramDeletePageAction"), m_service->pages().size() > 1);
    for (const char *raw : {ActionId::kEditSelectAll, ActionId::kEditFind, ActionId::kEditReplace,
                            ActionId::kEditFindNext, ActionId::kEditFindPrevious, ActionId::kViewZoomIn,
                            ActionId::kViewZoomOut, ActionId::kViewZoomReset}) {
        actions()->setEnabled(id(raw), true);
    }
}

void DiagramHost::syncChecks()
{
    const QVariantMap item = m_service->selected();
    const QVariantMap page = m_service->pageInfo();
    const auto check = [this](const QString &command, bool on) {
        if (Quick::ActionEntry *entry = action(command)) {
            if (entry->isChecked() != on) {
                entry->setChecked(on);
            }
        }
    };
    check(id("diagramFormatPainterAction"), m_service->formatPainter());
    check(id("diagramShadowAction"), item.value(QStringLiteral("shadow")).toBool());
    check(id("diagramRoundCornersAction"), item.value(QStringLiteral("corner")).toDouble() > 0);
    check(id("diagramLockAspectAction"), item.value(QStringLiteral("aspectLocked")).toBool());
    check(id("diagramLineJumpsAction"), page.value(QStringLiteral("lineJumps")).toBool());
    check(id("diagramShowGridAction"), page.value(QStringLiteral("grid")).toBool());
    const QString tool = m_service->tool();
    check(QStringLiteral("diagramTool%1Action").arg(tool.left(1).toUpper() + tool.mid(1)), true);
    const QString routing = item.value(QStringLiteral("routing")).toString();
    if (!routing.isEmpty()) {
        check(QStringLiteral("diagramRoute%1Action").arg(routing.left(1).toUpper() + routing.mid(1)), true);
    }
    const bool landscape = page.value(QStringLiteral("landscape")).toBool();
    check(landscape ? id("diagramLandscapeAction") : id("diagramPortraitAction"), true);
    const QString paper = page.value(QStringLiteral("paper")).toString();
    if (paper != QLatin1String("custom") && !paper.isEmpty()) {
        const QString name = paper.length() == 2 ? paper.toUpper() : paper.left(1).toUpper() + paper.mid(1);
        check(QStringLiteral("diagramPaper%1Action").arg(name), true);
    }
    check(QStringLiteral("diagramTheme_") + m_service->themeId(), true);
}

} // namespace QindaOffice::Apps::Diagram
