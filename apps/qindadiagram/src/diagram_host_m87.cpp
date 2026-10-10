// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramHost (M87, D-095): the commands of Visio's everyday drawing tools
// — swimlanes and containers, layers, links, comments, external data, the
// Re-Layout styles, AutoConnect, line-jump styles, page breaks, Auto Size,
// background pages and Borders & Titles — their wiring and their state.
//
// AGENT-CONTRACT (rule 5): every id below is new in M87 and permanent;
// each sits in the menu tree exactly once (diagram_host_menus.cpp) and on
// the ribbon (qml/DiagramRibbonLayout.qml). Insert ▸ Link is the suite's
// shared insertHyperlinkAction (Ctrl+K), as in Write and Calc.
#include "diagram_host.h"

#include "drawing_document.h"
#include "print_tiles.h"
#include "standard_actions.h"

#include <QKeySequence>
#include <QPageLayout>
#include <QPageSize>

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
    // Insert
    {"diagramInsertSwimlaneBeforeAction", QT_TRANSLATE_NOOP("DiagramHost", "Swimlane &Before"), "", "rows-3"},
    {"diagramInsertBackgroundPageAction", QT_TRANSLATE_NOOP("DiagramHost", "&Background Page"), "", "layers-2"},
    {ActionId::kInsertHyperlink, QT_TRANSLATE_NOOP("DiagramHost", "&Link…"), "Ctrl+K", "link"},
    {"diagramFollowLinkAction", QT_TRANSLATE_NOOP("DiagramHost", "&Open Link"), "", "external-link"},
    {"diagramBordersTitlesAction", QT_TRANSLATE_NOOP("DiagramHost", "Borders && &Titles"), "", "frame"},
    // Containers
    {"diagramContainerFitAction", QT_TRANSLATE_NOOP("DiagramHost", "&Fit to Contents"), "", "shrink"},
    {"diagramContainerDisbandAction", QT_TRANSLATE_NOOP("DiagramHost", "&Disband Container"), "", "ungroup"},
    {"diagramContainerSelectAction", QT_TRANSLATE_NOOP("DiagramHost", "&Select Contents"), "", "lasso-select"},
    // AutoConnect
    {"diagramConnectUpAction", QT_TRANSLATE_NOOP("DiagramHost", "Add and Connect &Above"), "", "arrow-up"},
    {"diagramConnectRightAction", QT_TRANSLATE_NOOP("DiagramHost", "Add and Connect &Right"), "", "arrow-right"},
    {"diagramConnectDownAction", QT_TRANSLATE_NOOP("DiagramHost", "Add and Connect &Below"), "", "arrow-down"},
    {"diagramConnectLeftAction", QT_TRANSLATE_NOOP("DiagramHost", "Add and Connect &Left"), "", "arrow-left"},
    // Re-Layout styles
    {"diagramLayoutBottomUpAction", QT_TRANSLATE_NOOP("DiagramHost", "Flowchart, &Bottom to Top"), "", "arrow-up"},
    {"diagramLayoutRightLeftAction", QT_TRANSLATE_NOOP("DiagramHost", "Flowchart, &Right to Left"), "", "arrow-left"},
    {"diagramLayoutCompactTreeAction", QT_TRANSLATE_NOOP("DiagramHost", "&Compact Tree"), "", "list-indent-increase"},
    {"diagramLayoutRadialAction", QT_TRANSLATE_NOOP("DiagramHost", "R&adial"), "", "target"},
    {"diagramLayoutCircularAction", QT_TRANSLATE_NOOP("DiagramHost", "C&ircular"), "", "circle"},
    {"diagramLayoutOptionsAction", QT_TRANSLATE_NOOP("DiagramHost", "Layout &Options…"), "", "sliders-horizontal"},
    // Layers
    {"diagramLayerPropertiesAction", QT_TRANSLATE_NOOP("DiagramHost", "&Layer Properties…"), "", "layers"},
    {"diagramAssignLayerAction", QT_TRANSLATE_NOOP("DiagramHost", "&Assign to Layer…"), "", "list-checks"},
    // Data
    {"diagramLinkDataAction", QT_TRANSLATE_NOOP("DiagramHost", "&Link Data to Shapes…"), "", "database"},
    {"diagramExternalDataAction", QT_TRANSLATE_NOOP("DiagramHost", "External &Data…"), "", "table"},
    {"diagramRefreshDataAction", QT_TRANSLATE_NOOP("DiagramHost", "&Refresh All"), "", "refresh-cw"},
    {"diagramUnlinkDataAction", QT_TRANSLATE_NOOP("DiagramHost", "&Unlink from Row"), "", "unlink"},
    // Review
    {"diagramNewCommentAction", QT_TRANSLATE_NOOP("DiagramHost", "&New Comment"), "Ctrl+Alt+M", "message-square-plus"},
    {"diagramDeleteCommentAction", QT_TRANSLATE_NOOP("DiagramHost", "&Delete Comment"), "", "message-square-x"},
    {"diagramPreviousCommentAction", QT_TRANSLATE_NOOP("DiagramHost", "&Previous Comment"), "", "chevron-up"},
    {"diagramNextCommentAction", QT_TRANSLATE_NOOP("DiagramHost", "Ne&xt Comment"), "", "chevron-down"},
};

constexpr Command kToggles[] = {
    {"diagramShowPageBreaksAction", QT_TRANSLATE_NOOP("DiagramHost", "Page &Breaks"), "", "grid-2x2"},
    {"diagramAutoConnectAction", QT_TRANSLATE_NOOP("DiagramHost", "&AutoConnect"), "", "route"},
    {"diagramShowCommentsAction", QT_TRANSLATE_NOOP("DiagramHost", "&Comments Pane"), "", "message-square"},
    {"diagramAutoSizeAction", QT_TRANSLATE_NOOP("DiagramHost", "&Auto Size"), "", "scaling"},
};

constexpr Command kResizes[] = {
    {"diagramContainerResizeNoneAction", QT_TRANSLATE_NOOP("DiagramHost", "&No Automatic Resize"), "", "lock"},
    {"diagramContainerResizeExpandAction", QT_TRANSLATE_NOOP("DiagramHost", "&Expand as Needed"), "", "expand"},
    {"diagramContainerResizeFitAction", QT_TRANSLATE_NOOP("DiagramHost", "Always &Fit to Contents"), "", "shrink"},
};
constexpr Command kJumpStyles[] = {
    {"diagramJumpArcAction", QT_TRANSLATE_NOOP("DiagramHost", "&Arc"), "", "spline"},
    {"diagramJumpGapAction", QT_TRANSLATE_NOOP("DiagramHost", "&Gap"), "", "separator-horizontal"},
    {"diagramJumpSquareAction", QT_TRANSLATE_NOOP("DiagramHost", "&Square"), "", "square"},
};
constexpr Command kJumpOn[] = {
    {"diagramJumpLastRoutedAction", QT_TRANSLATE_NOOP("DiagramHost", "On the &Last Routed Line"), "", "waypoints"},
    {"diagramJumpHorizontalAction", QT_TRANSLATE_NOOP("DiagramHost", "On &Horizontal Lines"), "", "move-horizontal"},
    {"diagramJumpVerticalAction", QT_TRANSLATE_NOOP("DiagramHost", "On &Vertical Lines"), "", "move-vertical"},
};

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

} // namespace

void DiagramHost::createM87Actions()
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
    radio(kResizes, "diagramContainerResize");
    radio(kJumpStyles, "diagramJumpStyle");
    radio(kJumpOn, "diagramJumpOn");
    action(id("diagramAutoConnectAction"))->setChecked(true); // Visio's default
}

void DiagramHost::wireM87Actions()
{
    DiagramService *d = m_service;
    const auto onId = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    const auto ask = [this](const char *which) { return [this, which] { emit dialogRequested(id(which)); }; };
    // Insert
    onId("diagramInsertSwimlaneBeforeAction", [this, d] {
        const bool vertical = d->selected().value(QStringLiteral("type")).toString() == QLatin1String("laneVertical");
        d->insertSwimlane(vertical ? QStringLiteral("laneVertical") : QStringLiteral("laneHorizontal"), true,
                          m_viewCenter.x(), m_viewCenter.y());
    });
    onId("diagramInsertBackgroundPageAction", [d] { d->addBackgroundPage(); });
    onId(ActionId::kInsertHyperlink, ask("link"));
    onId("diagramFollowLinkAction", [d] {
        if (!d->selection().isEmpty()) d->followLink(d->selection().first());
    });
    onId("diagramBordersTitlesAction", [d] { d->addBorderAndTitle(); });
    // Containers
    onId("diagramContainerFitAction", [d] { d->fitContainers(); });
    onId("diagramContainerDisbandAction", [d] { d->disbandContainers(); });
    onId("diagramContainerSelectAction", [d] { d->selectContainerContents(); });
    for (const auto &[command, resize] : {std::pair{"diagramContainerResizeNoneAction", "none"},
                                          {"diagramContainerResizeExpandAction", "expand"},
                                          {"diagramContainerResizeFitAction", "fit"}}) {
        const QString value = id(resize);
        onId(command, [d, value] { d->setContainerResize(value); });
    }
    // AutoConnect from the selected shape
    for (const auto &[command, side] : {std::pair{"diagramConnectUpAction", 0}, {"diagramConnectRightAction", 1},
                                        {"diagramConnectDownAction", 2}, {"diagramConnectLeftAction", 3}}) {
        const int s = side;
        onId(command, [d, s] {
            d->autoConnect(d->selection().isEmpty() ? QString() : d->selection().first(), s);
        });
    }
    // Re-Layout
    for (const auto &[command, kind] : {std::pair{"diagramLayoutBottomUpAction", "bottomUp"},
                                        {"diagramLayoutRightLeftAction", "rightLeft"},
                                        {"diagramLayoutCompactTreeAction", "compactTree"},
                                        {"diagramLayoutRadialAction", "radial"},
                                        {"diagramLayoutCircularAction", "circular"}}) {
        const QString k = id(kind);
        onId(command, [d, k] { d->autoLayout(k); });
    }
    onId("diagramLayoutOptionsAction", ask("layout"));
    // Layers, data, review
    onId("diagramLayerPropertiesAction", ask("layers"));
    onId("diagramAssignLayerAction", ask("assignLayers"));
    onId("diagramLinkDataAction", ask("linkData"));
    onId("diagramExternalDataAction", ask("externalData"));
    onId("diagramRefreshDataAction", [d] { d->refreshData(); });
    onId("diagramUnlinkDataAction", [d] { d->unlinkSelection(); });
    onId("diagramNewCommentAction", [this] {
        setCommentsPaneVisible(true);
        emit dialogRequested(QStringLiteral("newComment"));
    });
    onId("diagramDeleteCommentAction", [d] { d->deleteComment(); });
    onId("diagramPreviousCommentAction", [this, d] {
        setCommentsPaneVisible(true);
        d->stepComment(-1);
    });
    onId("diagramNextCommentAction", [this, d] {
        setCommentsPaneVisible(true);
        d->stepComment(1);
    });
    // Page: Auto Size and the line-jump style
    connect(action(id("diagramAutoSizeAction")), &ActionEntry::triggered, this, [d] {
        d->setAutoSize(!d->pageInfo().value(QStringLiteral("autoSize")).toBool());
    });
    for (const auto &[command, style] : {std::pair{"diagramJumpArcAction", "arc"}, {"diagramJumpGapAction", "gap"},
                                         {"diagramJumpSquareAction", "square"}}) {
        const QString s = id(style);
        onId(command, [d, s] { d->setLineJumpStyle(s, d->pageInfo().value(QStringLiteral("jumpOn")).toString()); });
    }
    for (const auto &[command, on] : {std::pair{"diagramJumpLastRoutedAction", "lastRouted"},
                                      {"diagramJumpHorizontalAction", "horizontal"},
                                      {"diagramJumpVerticalAction", "vertical"}}) {
        const QString o = id(on);
        onId(command, [d, o] { d->setLineJumpStyle(d->pageInfo().value(QStringLiteral("jumpStyle")).toString(), o); });
    }
    for (const char *toggle : {"diagramShowPageBreaksAction", "diagramAutoConnectAction", "diagramShowCommentsAction"}) {
        connect(action(id(toggle)), &ActionEntry::toggled, this, &DiagramHost::viewStateChanged);
    }
    connect(d, &DiagramService::activeCommentChanged, this, [this] { syncM87(); });
}

void DiagramHost::syncM87()
{
    const QVariantMap item = m_service->selected();
    const QVariantMap page = m_service->pageInfo();
    const QVariantMap container = m_service->containerInfo();
    const bool shape = item.value(QStringLiteral("kind")).toString() == QLatin1String("shape");
    const bool any = !m_service->selection().isEmpty();
    const auto set = [this](std::initializer_list<const char *> ids, bool enabled) {
        for (const char *raw : ids) {
            actions()->setEnabled(id(raw), enabled);
        }
    };
    set({"diagramContainerFitAction", "diagramContainerDisbandAction", "diagramContainerSelectAction",
         "diagramContainerResizeNoneAction", "diagramContainerResizeExpandAction", "diagramContainerResizeFitAction"},
        !container.isEmpty());
    set({"diagramConnectUpAction", "diagramConnectRightAction", "diagramConnectDownAction", "diagramConnectLeftAction"},
        shape && m_service->selection().size() == 1 && !item.value(QStringLiteral("container")).toBool());
    set({ActionId::kInsertHyperlink, "diagramAssignLayerAction"}, any);
    set({"diagramFollowLinkAction"}, item.value(QStringLiteral("linkCount")).toInt() > 0);
    set({"diagramUnlinkDataAction"}, item.value(QStringLiteral("dataLinked")).toBool());
    set({"diagramRefreshDataAction", "diagramExternalDataAction"}, !m_service->recordsetList().isEmpty());
    set({"diagramDeleteCommentAction"}, !m_service->activeComment().isEmpty());
    set({"diagramPreviousCommentAction", "diagramNextCommentAction"}, m_service->commentTotal() > 0);
    set({"diagramJumpArcAction", "diagramJumpGapAction", "diagramJumpSquareAction", "diagramJumpLastRoutedAction",
         "diagramJumpHorizontalAction", "diagramJumpVerticalAction"},
        true);
    const auto check = [this](const QString &command, bool on) {
        if (Quick::ActionEntry *entry = action(command); entry != nullptr && entry->isChecked() != on) {
            entry->setChecked(on);
        }
    };
    check(id("diagramAutoSizeAction"), page.value(QStringLiteral("autoSize")).toBool());
    const QString resize = container.value(QStringLiteral("resize")).toString();
    if (!resize.isEmpty()) {
        check(QStringLiteral("diagramContainerResize%1Action").arg(resize.left(1).toUpper() + resize.mid(1)), true);
    }
    const QString style = page.value(QStringLiteral("jumpStyle")).toString();
    if (!style.isEmpty()) {
        check(QStringLiteral("diagramJump%1Action").arg(style.left(1).toUpper() + style.mid(1)), true);
    }
    const QString on = page.value(QStringLiteral("jumpOn")).toString();
    if (!on.isEmpty()) {
        check(QStringLiteral("diagramJump%1Action").arg(on.left(1).toUpper() + on.mid(1)), true);
    }
}

bool DiagramHost::pageBreaksVisible() const
{
    return toggled("diagramShowPageBreaksAction");
}

bool DiagramHost::autoConnectEnabled() const
{
    return toggled("diagramAutoConnectAction");
}

bool DiagramHost::commentsPaneVisible() const
{
    return toggled("diagramShowCommentsAction");
}

void DiagramHost::setCommentsPaneVisible(bool visible)
{
    action(id("diagramShowCommentsAction"))->setChecked(visible);
}

std::optional<QPageLayout> DiagramHost::printPageLayout() const
{
    const auto &pages = m_document->drawing().pages;
    if (pages.isEmpty() || pages.first().print.paper.isEmpty()) {
        return std::nullopt; // the printer's own paper (M50's behaviour)
    }
    const QSizeF paper = pages.first().print.paper;
    const bool landscape = paper.width() > paper.height();
    const QSizeF portraitInches(std::min(paper.width(), paper.height()) / 96.0, std::max(paper.width(), paper.height()) / 96.0);
    const double margin = ::QindaOffice::Diagram::kPrintMargin / 96.0 * 72.0; // points
    return QPageLayout(QPageSize(portraitInches, QPageSize::Inch), landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                       QMarginsF(margin, margin, margin, margin), QPageLayout::Point);
}

} // namespace QindaOffice::Apps::Diagram
