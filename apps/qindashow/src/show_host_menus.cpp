// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's menus: Slide, Insert, Shape and Arrange inserted before the
// shell's AI menu; the snap toggle appended to View; the inline AI
// commands and deck generation appended to that one AI menu (never a
// second "AI"); New from Template; and the canvas context menu.

#include "show_host.h"

#include "show_arrange.h"
#include "show_design_actions.h"
#include "show_slide_layouts.h"
#include "show_table_actions.h"
#include "show_toolbox.h"
#include "standard_actions.h"
#include "template_loader.h"

#include "inline_action_flow.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

QString sanitizedId(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

void ShowHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    const auto make = [this](const char *id, const QString &text, const QKeySequence &shortcut = {},
                             const char *icon = nullptr) {
        return actions()->add(QString::fromLatin1(id), text, shortcut,
                              icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    // Persistent slide commands (rule 5), shared by the Slide menu and the
    // main strip.
    connect(make(ActionId::kSlideNew, tr("New slide"), QKeySequence(QStringLiteral("Ctrl+M")), "file-plus"),
            &ActionEntry::triggered, this, [this] { addSlide(); });
    connect(make(ActionId::kSlideDuplicate, tr("Duplicate slide"), {}, "copy"), &ActionEntry::triggered, this,
            [this] { duplicateSlide(); });
    connect(make(ActionId::kSlideDelete, tr("Delete slide"), {}, "trash"), &ActionEntry::triggered, this,
            [this] { deleteSlide(); });
    connect(make(ActionId::kSlideStartShow, tr("Start show"), QKeySequence(Qt::Key_F5), "play"),
            &ActionEntry::triggered, this, [this] { startShow(); });
    connect(actions()->add(QStringLiteral("showAppearOnClickAction"), tr("Appear on click")),
            &ActionEntry::triggered, m_canvas, &SlideCanvasItem::toggleBuildSelected);
    connect(actions()->add(QStringLiteral("showExportPdfAction"), tr("Export PDF…")), &ActionEntry::triggered,
            this, [this] {
                QString suggested = m_document->canonicalPath();
                suggested = suggested.isEmpty() ? tr("Untitled.pdf")
                                                : suggested.section(QLatin1Char('.'), 0, -2) + QStringLiteral(".pdf");
                emit exportPdfRequested(suggested);
            });

    QList<MenuNode> slideItems{a(ActionId::kSlideNew), a(ActionId::kSlideDuplicate), a(ActionId::kSlideDelete),
                               MenuNode::separator()};
    // M82 (D-091): New Slide on a layout, Layout, Reset, Reuse Slides.
    slideItems += m_layouts->menuItems();
    slideItems.append(MenuNode::separator());
    slideItems += m_arrange->slideMenuItems();
    slideItems.append(MenuNode::separator());
    slideItems += m_design->slideMenuItems();
    slideItems += {MenuNode::separator(), a(ActionId::kSlideStartShow), MenuNode::action(QStringLiteral("showPresenterViewAction")),
                   MenuNode::separator(), MenuNode::action(QStringLiteral("showExportPdfAction"))};
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showSlideMenu"), tr("&Slide"), slideItems));

    QList<MenuNode> insertItems = m_toolbox->insertMenuItems();
    insertItems += {MenuNode::action(QString::fromLatin1(TableActionId::kInsertTable)), MenuNode::separator(),
                    MenuNode::action(QStringLiteral("showAppearOnClickAction"))};
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showInsertMenu"), tr("&Insert"), insertItems));

    QList<MenuNode> shapeItems = m_toolbox->styleMenuItems();
    shapeItems += m_design->shapeMenuItems();
    shapeItems += chartMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showShapeMenu"), tr("S&hape"), shapeItems));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showArrangeMenu"), tr("&Arrange"),
                                             m_arrange->arrangeMenuItems()));
    // M67: the table commands (the ribbon's Table Design / Table Layout tabs).
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showTableMenu"), tr("&Table"), m_tables->tableMenuItems()));
    tree->appendTo(QStringLiteral("viewMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("viewMenu"), MenuNode::action(QStringLiteral("showViewNotesAction")));
    for (const MenuNode &node : m_design->viewMenuItems()) {
        tree->appendTo(QStringLiteral("viewMenu"), node);
    }

    // AGENT-GUARD: the inline AI commands belong in the SHELL's AI menu,
    // never in a second menu of the same title.
    tree->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    for (const Ai::InlineAction &inlineAction : Ai::inlineActions()) {
        const QString id = QStringLiteral("showAi%1").arg(inlineAction.id);
        connect(actions()->add(id, inlineAction.displayName), &ActionEntry::triggered, this,
                [this, actionId = inlineAction.id] { m_inline->run(actionId); });
        tree->appendTo(QStringLiteral("aiMenu"), MenuNode::action(id));
    }
    tree->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    connect(actions()->add(QStringLiteral("showGenerateDeckAction"), tr("Generate deck from outline…")),
            &ActionEntry::triggered, this, [this] { emit generateDeckRequested(); });
    tree->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QStringLiteral("showGenerateDeckAction")));

    // New from Template: one entry per shipped template, generated ids
    // (showTemplate_<name>) that stay stable (plan rule 4).
    const QList<Quick::TemplateEntry> templates = Quick::templatesFor(QStringLiteral("QindaShow"));
    if (!templates.isEmpty()) {
        MenuNode submenu = MenuNode::submenu(QStringLiteral("fileTemplateMenu"), tr("New from &Template"));
        for (const Quick::TemplateEntry &entry : templates) {
            const QString id = QStringLiteral("showTemplate_%1").arg(sanitizedId(entry.name));
            connect(actions()->add(id, entry.name), &ActionEntry::triggered, this,
                    [this, path = entry.path] { loadTemplate(path); });
            submenu.children.append(MenuNode::action(id));
        }
        tree->insertInto(QStringLiteral("fileMenu"), QString::fromLatin1(ActionId::kFileCloseWindow), submenu);
    }
}

QVariantMap ShowHost::contextMenuNode() const
{
    QList<MenuNode> children = m_arrange->contextMenuItems();
    children += m_design->contextMenuItems();
    children += {MenuNode::separator(), a(ActionId::kEditCut), a(ActionId::kEditCopy), a(ActionId::kEditPaste)};
    return MenuNode::submenu(QStringLiteral("showCanvasContextMenu"), QString(), children).toVariant();
}

} // namespace QindaOffice::Apps::Show
