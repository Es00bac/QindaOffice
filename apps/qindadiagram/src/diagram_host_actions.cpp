// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramHost: the QindaDiagram commands (ActionEntries) and what they do.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon and the keyboard — never rename
// or reuse one; add new ids for new commands. Each id appears in the menu
// tree exactly once (diagram_host_menus.cpp; the desktop validator rejects
// duplicates). Shortcuts follow Visio where it has one (Ctrl+1/2/3 tools,
// Ctrl+D duplicate, Ctrl+G group, Ctrl+R/L rotate, Ctrl+Alt+H/J flip (Ctrl+H is
// the suite's Replace),
// Ctrl+Shift+W fit page) and never shadow the suite's standard chords.
#include "diagram_host.h"

#include "standard_actions.h"
#include "themes.h"

#include <QKeySequence>

namespace QindaOffice::Apps::Diagram {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
};

constexpr Command kCommands[] = {
    // Editing
    {"diagramDuplicateAction", QT_TRANSLATE_NOOP("DiagramHost", "&Duplicate"), "Ctrl+D", "copy-plus"},
    {"diagramDeleteAction", QT_TRANSLATE_NOOP("DiagramHost", "De&lete"), "", "trash-2"},
    {"diagramEditTextAction", QT_TRANSLATE_NOOP("DiagramHost", "Edit &Text"), "F2", "text-cursor-input"},
    {"diagramShapeDataAction", QT_TRANSLATE_NOOP("DiagramHost", "Shape &Data…"), "", "table-2"},
    {"diagramCopyAsImageAction", QT_TRANSLATE_NOOP("DiagramHost", "Copy as &Picture"), "Ctrl+Alt+C", "images"},
    // Text formatting (the shared rich-text ids where the meaning is the same)
    {ActionId::kFormatBold, QT_TRANSLATE_NOOP("DiagramHost", "&Bold"), "Ctrl+B", "bold"},
    {ActionId::kFormatItalic, QT_TRANSLATE_NOOP("DiagramHost", "&Italic"), "Ctrl+I", "italic"},
    {ActionId::kFormatUnderline, QT_TRANSLATE_NOOP("DiagramHost", "&Underline"), "Ctrl+U", "underline"},
    {ActionId::kFormatGrowFont, QT_TRANSLATE_NOOP("DiagramHost", "&Grow Font"), "Ctrl+]", "a-large-small"},
    {ActionId::kFormatShrinkFont, QT_TRANSLATE_NOOP("DiagramHost", "S&hrink Font"), "Ctrl+[", "baseline"},
    {ActionId::kFormatTextColor, QT_TRANSLATE_NOOP("DiagramHost", "Text &Color…"), "", "type-outline"},
    {ActionId::kFormatAlignLeft, QT_TRANSLATE_NOOP("DiagramHost", "Align Text &Left"), "Ctrl+Shift+L", "text-align-start"},
    {ActionId::kFormatAlignCenter, QT_TRANSLATE_NOOP("DiagramHost", "&Center Text"), "Ctrl+Shift+E", "text-align-center"},
    {ActionId::kFormatAlignRight, QT_TRANSLATE_NOOP("DiagramHost", "Align Text &Right"), "Ctrl+Shift+R", "text-align-end"},
    {"diagramTextTopAction", QT_TRANSLATE_NOOP("DiagramHost", "Text at &Top"), "", "arrow-up-to-line"},
    {"diagramTextMiddleAction", QT_TRANSLATE_NOOP("DiagramHost", "Text in the &Middle"), "", "align-vertical-justify-center"},
    {"diagramTextBottomAction", QT_TRANSLATE_NOOP("DiagramHost", "Text at the Botto&m"), "", "arrow-down-to-line"},
    // Shape styles
    {"diagramFillColorAction", QT_TRANSLATE_NOOP("DiagramHost", "&Fill Color…"), "", "paint-bucket"},
    {"diagramLineColorAction", QT_TRANSLATE_NOOP("DiagramHost", "&Line Color…"), "", "pen-tool"},
    {"diagramLineWeight1Action", QT_TRANSLATE_NOOP("DiagramHost", "Hairline (0.75 pt)"), "", "minus"},
    {"diagramLineWeight2Action", QT_TRANSLATE_NOOP("DiagramHost", "Thin (1.5 pt)"), "", "minus"},
    {"diagramLineWeight3Action", QT_TRANSLATE_NOOP("DiagramHost", "Medium (2.25 pt)"), "", "minus"},
    {"diagramLineWeight4Action", QT_TRANSLATE_NOOP("DiagramHost", "Thick (3 pt)"), "", "minus"},
    {"diagramLineSolidAction", QT_TRANSLATE_NOOP("DiagramHost", "Solid Line"), "", "minus"},
    {"diagramLineDashAction", QT_TRANSLATE_NOOP("DiagramHost", "Dashed Line"), "", "square-dashed"},
    {"diagramLineDotAction", QT_TRANSLATE_NOOP("DiagramHost", "Dotted Line"), "", "circle-dot"},
    {"diagramQuickStyle1Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Primary"), "", "palette"},
    {"diagramQuickStyle2Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Warm"), "", "palette"},
    {"diagramQuickStyle3Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Positive"), "", "palette"},
    {"diagramQuickStyle4Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Data"), "", "palette"},
    {"diagramQuickStyle5Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Neutral"), "", "palette"},
    {"diagramQuickStyle6Action", QT_TRANSLATE_NOOP("DiagramHost", "Quick Style: Alert"), "", "palette"},
    // Arrange
    {"diagramAlignLeftAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Left"), "", "align-start-vertical"},
    {"diagramAlignCenterAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Center"), "", "align-center-vertical"},
    {"diagramAlignRightAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Right"), "", "align-end-vertical"},
    {"diagramAlignTopAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Top"), "", "align-start-horizontal"},
    {"diagramAlignMiddleAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Middle"), "", "align-center-horizontal"},
    {"diagramAlignBottomAction", QT_TRANSLATE_NOOP("DiagramHost", "Align &Bottom"), "", "align-end-horizontal"},
    {"diagramDistributeHorizontalAction", QT_TRANSLATE_NOOP("DiagramHost", "Distribute &Horizontally"), "", "align-horizontal-space-between"},
    {"diagramDistributeVerticalAction", QT_TRANSLATE_NOOP("DiagramHost", "Distribute &Vertically"), "", "align-vertical-space-between"},
    {"diagramBringToFrontAction", QT_TRANSLATE_NOOP("DiagramHost", "Bring to &Front"), "Ctrl+Shift+F", "bring-to-front"},
    {"diagramBringForwardAction", QT_TRANSLATE_NOOP("DiagramHost", "Bring F&orward"), "", "arrow-up"},
    {"diagramSendBackwardAction", QT_TRANSLATE_NOOP("DiagramHost", "Send Bac&kward"), "", "arrow-down"},
    {"diagramSendToBackAction", QT_TRANSLATE_NOOP("DiagramHost", "Send to &Back"), "Ctrl+Shift+B", "send-to-back"},
    {"diagramGroupAction", QT_TRANSLATE_NOOP("DiagramHost", "&Group"), "Ctrl+G", "group"},
    {"diagramUngroupAction", QT_TRANSLATE_NOOP("DiagramHost", "&Ungroup"), "Ctrl+Shift+G", "ungroup"},
    {"diagramRotateRightAction", QT_TRANSLATE_NOOP("DiagramHost", "Rotate &Right 90°"), "Ctrl+R", "rotate-cw"},
    {"diagramRotateLeftAction", QT_TRANSLATE_NOOP("DiagramHost", "Rotate L&eft 90°"), "Ctrl+L", "rotate-ccw"},
    {"diagramFlipHorizontalAction", QT_TRANSLATE_NOOP("DiagramHost", "Flip &Horizontal"), "Ctrl+Alt+H", "move-horizontal"},
    {"diagramFlipVerticalAction", QT_TRANSLATE_NOOP("DiagramHost", "Flip &Vertical"), "Ctrl+Alt+J", "move-vertical"},
    {"diagramLayoutTopDownAction", QT_TRANSLATE_NOOP("DiagramHost", "Flowchart, &Top to Bottom"), "", "workflow"},
    {"diagramLayoutLeftRightAction", QT_TRANSLATE_NOOP("DiagramHost", "Flowchart, &Left to Right"), "", "arrow-right"},
    {"diagramLayoutTreeAction", QT_TRANSLATE_NOOP("DiagramHost", "&Org Chart (Tree)"), "", "list-tree"},
    // Connectors
    {"diagramArrowNoneAction", QT_TRANSLATE_NOOP("DiagramHost", "No Arrows"), "", "minus"},
    {"diagramArrowEndAction", QT_TRANSLATE_NOOP("DiagramHost", "Arrow at End"), "", "arrow-right"},
    {"diagramArrowStartAction", QT_TRANSLATE_NOOP("DiagramHost", "Arrow at Start"), "", "arrow-left"},
    {"diagramArrowBothAction", QT_TRANSLATE_NOOP("DiagramHost", "Arrows at Both Ends"), "", "arrow-left-right"},
    // Pages and inserts
    {"diagramNewPageAction", QT_TRANSLATE_NOOP("DiagramHost", "New &Page"), "Ctrl+Shift+N", "file-plus"},
    {"diagramDuplicatePageAction", QT_TRANSLATE_NOOP("DiagramHost", "D&uplicate Page"), "", "copy"},
    {"diagramDeletePageAction", QT_TRANSLATE_NOOP("DiagramHost", "Delete Pa&ge"), "", "trash"},
    {"diagramRenamePageAction", QT_TRANSLATE_NOOP("DiagramHost", "&Rename Page…"), "", "pencil"},
    {"diagramInsertTextBoxAction", QT_TRANSLATE_NOOP("DiagramHost", "&Text Box"), "", "text-cursor"},
    {"diagramInsertPictureAction", QT_TRANSLATE_NOOP("DiagramHost", "&Picture…"), "", "image"},
    {"diagramInsertContainerAction", QT_TRANSLATE_NOOP("DiagramHost", "&Container"), "", "square-dashed"},
    {"diagramInsertSwimlaneAction", QT_TRANSLATE_NOOP("DiagramHost", "&Swimlane"), "", "rows-3"},
    {"diagramInsertVerticalLaneAction", QT_TRANSLATE_NOOP("DiagramHost", "&Vertical Swimlane"), "", "columns-3"},
    {"diagramInsertPhaseAction", QT_TRANSLATE_NOOP("DiagramHost", "P&hase"), "", "separator-vertical"},
    {"diagramInsertCalloutAction", QT_TRANSLATE_NOOP("DiagramHost", "Call&out"), "", "message-square"},
    {"diagramInsertNoteAction", QT_TRANSLATE_NOOP("DiagramHost", "&Note"), "", "sticky-note"},
    {"diagramOrgChartFromCsvAction", QT_TRANSLATE_NOOP("DiagramHost", "Org Chart from &CSV…"), "", "users"},
    // File
    {"diagramImportDrawioAction", QT_TRANSLATE_NOOP("DiagramHost", "Import &draw.io…"), "", "import"},
    {"diagramExportPngAction", QT_TRANSLATE_NOOP("DiagramHost", "Export as P&NG…"), "", "file-image"},
    {"diagramExportSvgAction", QT_TRANSLATE_NOOP("DiagramHost", "Export as &SVG…"), "", "file-output"},
    {"diagramExportPdfAction", QT_TRANSLATE_NOOP("DiagramHost", "Export as P&DF…"), "", "file-down"},
    // Design
    {"diagramPageSetupAction", QT_TRANSLATE_NOOP("DiagramHost", "Page &Setup…"), "", "layout-template"},
    {"diagramFitPageAction", QT_TRANSLATE_NOOP("DiagramHost", "&Fit Page to Drawing"), "", "shrink"},
    {"diagramBackgroundColorAction", QT_TRANSLATE_NOOP("DiagramHost", "Page &Background…"), "", "paint-roller"},
    // View
    {"diagramZoomFitPageAction", QT_TRANSLATE_NOOP("DiagramHost", "Fit &Page"), "Ctrl+Shift+W", "maximize"},
    {"diagramZoomSelectionAction", QT_TRANSLATE_NOOP("DiagramHost", "Zoom to &Selection"), "", "focus"},
};

// Checkable commands: view toggles, format toggles and page settings.
constexpr Command kToggles[] = {
    {"diagramFormatPainterAction", QT_TRANSLATE_NOOP("DiagramHost", "Format &Painter"), "Ctrl+Shift+C", "paintbrush"},
    {"diagramShadowAction", QT_TRANSLATE_NOOP("DiagramHost", "&Shadow"), "", "square-stack"},
    {"diagramRoundCornersAction", QT_TRANSLATE_NOOP("DiagramHost", "&Rounded Corners"), "", "square-round-corner"},
    {"diagramLockAspectAction", QT_TRANSLATE_NOOP("DiagramHost", "Lock &Aspect Ratio"), "", "lock"},
    {"diagramLineJumpsAction", QT_TRANSLATE_NOOP("DiagramHost", "Line &Jumps"), "", "waypoints"},
    {"diagramShowGridAction", QT_TRANSLATE_NOOP("DiagramHost", "&Grid"), "", "grid-3x3"},
    {"diagramShowRulersAction", QT_TRANSLATE_NOOP("DiagramHost", "&Rulers"), "", "ruler"},
    {"diagramSnapAction", QT_TRANSLATE_NOOP("DiagramHost", "S&nap and Glue"), "", "magnet"},
    {"diagramShowShapesPanelAction", QT_TRANSLATE_NOOP("DiagramHost", "S&hapes Panel"), "", "panel-left"},
    {"diagramShowInspectorAction", QT_TRANSLATE_NOOP("DiagramHost", "&Inspector"), "", "panel-right"},
    {"diagramShowMinimapAction", QT_TRANSLATE_NOOP("DiagramHost", "&Minimap"), "", "scan-eye"},
};

// Radio groups: {group, commands…}.
constexpr Command kTools[] = {
    {"diagramToolPointerAction", QT_TRANSLATE_NOOP("DiagramHost", "&Pointer Tool"), "Ctrl+1", "mouse-pointer-2"},
    {"diagramToolTextAction", QT_TRANSLATE_NOOP("DiagramHost", "&Text Tool"), "Ctrl+2", "type"},
    {"diagramToolConnectorAction", QT_TRANSLATE_NOOP("DiagramHost", "&Connector Tool"), "Ctrl+3", "spline"},
};
constexpr Command kRoutings[] = {
    {"diagramRouteOrthogonalAction", QT_TRANSLATE_NOOP("DiagramHost", "&Right-Angle Connector"), "", "corner-down-right"},
    {"diagramRouteStraightAction", QT_TRANSLATE_NOOP("DiagramHost", "&Straight Connector"), "", "move-diagonal-2"},
    {"diagramRouteCurvedAction", QT_TRANSLATE_NOOP("DiagramHost", "&Curved Connector"), "", "spline"},
};
constexpr Command kOrientations[] = {
    {"diagramPortraitAction", QT_TRANSLATE_NOOP("DiagramHost", "&Portrait"), "", "file"},
    {"diagramLandscapeAction", QT_TRANSLATE_NOOP("DiagramHost", "&Landscape"), "", "monitor"},
};
constexpr Command kPapers[] = {
    {"diagramPaperLetterAction", QT_TRANSLATE_NOOP("DiagramHost", "Letter"), "", ""},
    {"diagramPaperLegalAction", QT_TRANSLATE_NOOP("DiagramHost", "Legal"), "", ""},
    {"diagramPaperTabloidAction", QT_TRANSLATE_NOOP("DiagramHost", "Tabloid"), "", ""},
    {"diagramPaperA5Action", QT_TRANSLATE_NOOP("DiagramHost", "A5"), "", ""},
    {"diagramPaperA4Action", QT_TRANSLATE_NOOP("DiagramHost", "A4"), "", ""},
    {"diagramPaperA3Action", QT_TRANSLATE_NOOP("DiagramHost", "A3"), "", ""},
};

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

} // namespace

void DiagramHost::createActions()
{
    const auto add = [this](const Command &c) {
        return actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
    };
    for (const Command &c : kCommands) {
        add(c);
    }
    for (const Command &c : kToggles) {
        add(c)->setCheckable(true);
    }
    const auto radio = [&](const auto &table, const char *group) {
        for (const Command &c : table) {
            ActionEntry *entry = add(c);
            entry->setCheckable(true);
            entry->setRadioGroup(id(group));
        }
    };
    radio(kTools, "diagramTool");
    radio(kRoutings, "diagramRouting");
    radio(kOrientations, "diagramOrientation");
    radio(kPapers, "diagramPaper");
    for (const auto &t : ::QindaOffice::Diagram::themes()) {
        ActionEntry *entry = actions()->add(QStringLiteral("diagramTheme_") + t.id, t.name, {}, QStringLiteral("palette"));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("diagramTheme"));
    }
    action(id("diagramToolPointerAction"))->setChecked(true);
    // The minimap starts closed (Visio's Pan & Zoom does too): at page fit it
    // would cover the page's corner.
    for (const char *on : {"diagramShowRulersAction", "diagramSnapAction", "diagramShowShapesPanelAction",
                           "diagramShowInspectorAction"}) {
        action(id(on))->setChecked(true);
    }
    action(id(ActionId::kFileNew))->setText(tr("&New Drawing"));
}

void DiagramHost::wireActions()
{
    const auto on = [this](const QString &command, auto &&slot) {
        connect(action(command), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    const auto onId = [&](const char *raw, auto &&slot) { on(id(raw), std::forward<decltype(slot)>(slot)); };
    DiagramService *d = m_service;
    // Standard edit commands.
    onId(ActionId::kEditCut, [d] { d->cut(); });
    onId(ActionId::kEditCopy, [d] { d->copy(); });
    onId(ActionId::kEditPaste, [d] { d->paste(); });
    onId(ActionId::kEditSelectAll, [d] { d->selectAll(); });
    onId(ActionId::kEditFind, [this] { emit findRequested(); });
    onId(ActionId::kEditReplace, [this] { emit replaceRequested(); });
    onId(ActionId::kEditFindNext, [d] { d->findNext(1); });
    onId(ActionId::kEditFindPrevious, [d] { d->findNext(-1); });
    onId(ActionId::kViewZoomIn, [this] { emit zoomRequested(QStringLiteral("in")); });
    onId(ActionId::kViewZoomOut, [this] { emit zoomRequested(QStringLiteral("out")); });
    onId(ActionId::kViewZoomReset, [this] { emit zoomRequested(QStringLiteral("reset")); });
    onId("diagramZoomFitPageAction", [this] { emit zoomRequested(QStringLiteral("page")); });
    onId("diagramZoomSelectionAction", [this] { emit zoomRequested(QStringLiteral("selection")); });
    // Editing
    onId("diagramDuplicateAction", [d] { d->duplicateSelection(); });
    onId("diagramDeleteAction", [d] { d->deleteSelection(); });
    onId("diagramEditTextAction", [d] {
        if (!d->selection().isEmpty()) emit d->textEditRequested(d->selection().first());
    });
    onId("diagramShapeDataAction", [this] { setInspectorVisible(true); emit inspectorRequested(QStringLiteral("data")); });
    onId("diagramCopyAsImageAction", [d] { d->copyAsImage(); });
    connect(action(id("diagramFormatPainterAction")), &ActionEntry::toggled, d, &DiagramService::setFormatPainter);
    // Text
    const auto text = [d](const char *key, const QVariant &value) { d->setTextStyle({{QString::fromLatin1(key), value}}); };
    onId(ActionId::kFormatBold, [d, text] { text("bold", !d->selected().value(QStringLiteral("textStyle")).toMap().value(QStringLiteral("bold")).toBool()); });
    onId(ActionId::kFormatItalic, [d, text] { text("italic", !d->selected().value(QStringLiteral("textStyle")).toMap().value(QStringLiteral("italic")).toBool()); });
    onId(ActionId::kFormatUnderline, [d, text] { text("underline", !d->selected().value(QStringLiteral("textStyle")).toMap().value(QStringLiteral("underline")).toBool()); });
    onId(ActionId::kFormatGrowFont, [text] { text("grow", 1); });
    onId(ActionId::kFormatShrinkFont, [text] { text("grow", -1); });
    onId(ActionId::kFormatTextColor, [this] { emit colorRequested(QStringLiteral("text")); });
    onId(ActionId::kFormatAlignLeft, [text] { text("align", QStringLiteral("left")); });
    onId(ActionId::kFormatAlignCenter, [text] { text("align", QStringLiteral("center")); });
    onId(ActionId::kFormatAlignRight, [text] { text("align", QStringLiteral("right")); });
    onId("diagramTextTopAction", [text] { text("valign", QStringLiteral("top")); });
    onId("diagramTextMiddleAction", [text] { text("valign", QStringLiteral("middle")); });
    onId("diagramTextBottomAction", [text] { text("valign", QStringLiteral("bottom")); });
    // Styles
    onId("diagramFillColorAction", [this] { emit colorRequested(QStringLiteral("fill")); });
    onId("diagramLineColorAction", [this] { emit colorRequested(QStringLiteral("line")); });
    const double weights[] = {0.75, 1.5, 2.25, 3.0};
    for (int i = 0; i < 4; ++i) {
        const double w = weights[i] * 96.0 / 72.0;
        on(QStringLiteral("diagramLineWeight%1Action").arg(i + 1), [d, w] { d->setStyle({{QStringLiteral("lineWidth"), w}}); });
    }
    onId("diagramLineSolidAction", [d] { d->setStyle({{QStringLiteral("pattern"), QStringLiteral("solid")}}); });
    onId("diagramLineDashAction", [d] { d->setStyle({{QStringLiteral("pattern"), QStringLiteral("dash")}}); });
    onId("diagramLineDotAction", [d] { d->setStyle({{QStringLiteral("pattern"), QStringLiteral("dot")}}); });
    for (int i = 0; i < 6; ++i) {
        on(QStringLiteral("diagramQuickStyle%1Action").arg(i + 1), [d, i] { d->quickStyle(i == 5 ? 5 : i); });
    }
    onId("diagramShadowAction", [d] { d->setStyle({{QStringLiteral("shadow"), !d->selected().value(QStringLiteral("shadow")).toBool()}}); });
    onId("diagramRoundCornersAction", [d] {
        const bool rounded = d->selected().value(QStringLiteral("corner")).toDouble() > 0;
        d->setStyle({{QStringLiteral("corner"), rounded ? 0.0 : 10.0}});
    });
    onId("diagramLockAspectAction", [d] { d->setAspectLocked(!d->selected().value(QStringLiteral("aspectLocked")).toBool()); });
    // Arrange
    for (const auto &[command, edge] : {std::pair{"diagramAlignLeftAction", "left"}, {"diagramAlignCenterAction", "center"},
                                        {"diagramAlignRightAction", "right"}, {"diagramAlignTopAction", "top"},
                                        {"diagramAlignMiddleAction", "middle"}, {"diagramAlignBottomAction", "bottom"}}) {
        const QString e = id(edge);
        onId(command, [d, e] { d->align(e); });
    }
    onId("diagramDistributeHorizontalAction", [d] { d->distribute(QStringLiteral("horizontal")); });
    onId("diagramDistributeVerticalAction", [d] { d->distribute(QStringLiteral("vertical")); });
    onId("diagramBringToFrontAction", [d] { d->order(QStringLiteral("front")); });
    onId("diagramBringForwardAction", [d] { d->order(QStringLiteral("forward")); });
    onId("diagramSendBackwardAction", [d] { d->order(QStringLiteral("backward")); });
    onId("diagramSendToBackAction", [d] { d->order(QStringLiteral("back")); });
    onId("diagramGroupAction", [d] { d->group(); });
    onId("diagramUngroupAction", [d] { d->ungroup(); });
    onId("diagramRotateRightAction", [d] { d->rotateSelection(90); });
    onId("diagramRotateLeftAction", [d] { d->rotateSelection(-90); });
    onId("diagramFlipHorizontalAction", [d] { d->flipSelection(QStringLiteral("horizontal")); });
    onId("diagramFlipVerticalAction", [d] { d->flipSelection(QStringLiteral("vertical")); });
    onId("diagramLayoutTopDownAction", [d] { d->autoLayout(QStringLiteral("topDown")); });
    onId("diagramLayoutLeftRightAction", [d] { d->autoLayout(QStringLiteral("leftRight")); });
    onId("diagramLayoutTreeAction", [d] { d->autoLayout(QStringLiteral("tree")); });
    // Connectors
    const auto connectorStyle = [d](const char *key, const char *value) {
        d->setConnectorStyle({{QString::fromLatin1(key), QString::fromLatin1(value)}});
    };
    onId("diagramRouteOrthogonalAction", [connectorStyle] { connectorStyle("routing", "orthogonal"); });
    onId("diagramRouteStraightAction", [connectorStyle] { connectorStyle("routing", "straight"); });
    onId("diagramRouteCurvedAction", [connectorStyle] { connectorStyle("routing", "curved"); });
    const auto arrows = [d](const char *start, const char *end) {
        d->setConnectorStyle({{QStringLiteral("startArrow"), id(start)}, {QStringLiteral("endArrow"), id(end)}});
    };
    onId("diagramArrowNoneAction", [arrows] { arrows("none", "none"); });
    onId("diagramArrowEndAction", [arrows] { arrows("none", "filled"); });
    onId("diagramArrowStartAction", [arrows] { arrows("filled", "none"); });
    onId("diagramArrowBothAction", [arrows] { arrows("filled", "filled"); });
    onId("diagramLineJumpsAction", [d] { d->setLineJumps(!d->pageInfo().value(QStringLiteral("lineJumps")).toBool()); });
    // Pages and inserts
    onId("diagramNewPageAction", [d] { d->addPage(); });
    onId("diagramDuplicatePageAction", [d] { d->duplicatePage(); });
    onId("diagramDeletePageAction", [d] { d->deletePage(); });
    onId("diagramRenamePageAction", [this] { emit renamePageRequested(); });
    for (const auto &[command, stencil] : {std::pair{"diagramInsertTextBoxAction", "text"},
                                           {"diagramInsertContainerAction", "container"},
                                           {"diagramInsertSwimlaneAction", "laneHorizontal"},
                                           {"diagramInsertVerticalLaneAction", "laneVertical"},
                                           {"diagramInsertPhaseAction", "phase"},
                                           {"diagramInsertCalloutAction", "calloutRect"},
                                           {"diagramInsertNoteAction", "note"}}) {
        const QString type = id(stencil);
        onId(command, [this, type] { m_service->addShape(type, m_viewCenter.x(), m_viewCenter.y()); });
    }
    onId("diagramInsertPictureAction", [this] { emit openFileRequested(QStringLiteral("picture")); });
    onId("diagramOrgChartFromCsvAction", [this] { emit openFileRequested(QStringLiteral("orgChart")); });
    onId("diagramImportDrawioAction", [this] { emit openFileRequested(QStringLiteral("drawio")); });
    onId("diagramExportPngAction", [this] { emit exportRequested(QStringLiteral("png")); });
    onId("diagramExportSvgAction", [this] { emit exportRequested(QStringLiteral("svg")); });
    onId("diagramExportPdfAction", [this] { emit exportRequested(QStringLiteral("pdf")); });
    // Design
    onId("diagramPageSetupAction", [this] { emit pageSetupRequested(); });
    onId("diagramFitPageAction", [d] { d->fitPageToContents(); });
    onId("diagramBackgroundColorAction", [this] { emit colorRequested(QStringLiteral("background")); });
    onId("diagramPortraitAction", [d] { d->setLandscape(false); });
    onId("diagramLandscapeAction", [d] { d->setLandscape(true); });
    for (const Command &c : kPapers) {
        const QString paper = id(c.text).toLower();
        onId(c.id, [d, paper] { d->setPaper(paper); });
    }
    for (const auto &t : ::QindaOffice::Diagram::themes()) {
        const QString theme = t.id;
        on(QStringLiteral("diagramTheme_") + t.id, [d, theme] { d->applyTheme(theme); });
    }
    onId("diagramShowGridAction", [d] { d->setGridVisible(!d->pageInfo().value(QStringLiteral("grid")).toBool()); });
    // Tools (radio) and view toggles.
    for (const auto &[command, tool] : {std::pair{"diagramToolPointerAction", "pointer"}, {"diagramToolTextAction", "text"},
                                        {"diagramToolConnectorAction", "connector"}}) {
        const QString name = id(tool);
        connect(action(id(command)), &ActionEntry::toggled, this, [d, name](bool checked) {
            if (checked) d->setTool(name);
        });
    }
    for (const char *toggle : {"diagramShowRulersAction", "diagramSnapAction", "diagramShowShapesPanelAction",
                               "diagramShowInspectorAction", "diagramShowMinimapAction"}) {
        connect(action(id(toggle)), &ActionEntry::toggled, this, &DiagramHost::viewStateChanged);
    }
}

} // namespace QindaOffice::Apps::Diagram
