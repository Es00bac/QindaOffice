// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's M75 (D-084) part: the Design tab, Format Background, the Slide
// Master view, sections, comments, audio and video (and M82's slide
// commands on the deck's layouts) — their controllers,
// their menus (the Design and Review menus; Section in the Slide menu,
// Video and Audio in Insert, Playback in Shape, Slide Master in View), and
// the whole-deck change every one of them pushes as one undo step.

#include "show_host.h"

#include "sections.h"
#include "show_background.h"
#include "show_commands.h"
#include "show_comments.h"
#include "show_design_tab.h"
#include "show_master_view.h"
#include "show_media_actions.h"
#include "show_sections.h"
#include "show_slide_layouts.h"

#include "menu_tree.h"

#include <QUndoStack>

namespace QindaOffice::Apps::Show {

using Quick::MenuNode;

void ShowHost::createDesign()
{
    m_masterView = new ShowMasterView(this);
    m_background = new ShowBackground(this);
    m_designTab = new ShowDesignTab(this);
    m_sections = new ShowSections(this);
    m_comments = new ShowComments(this);
    m_media = new ShowMedia(this);
    // M82: before insertMenus(), which puts its New Slide and Layout menus
    // in the Slide menu.
    m_layouts = new ShowSlideLayouts(this);
}

void ShowHost::designMenus()
{
    Quick::MenuTree *tree = menuTree();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showDesignMenu"), tr("&Design"), m_designTab->menuItems()));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("showReviewMenu"), tr("&Review"), m_comments->menuItems()));
    tree->appendTo(QStringLiteral("showSlideMenu"), MenuNode::separator());
    for (const MenuNode &node : m_sections->menuItems()) {
        tree->appendTo(QStringLiteral("showSlideMenu"), node);
    }
    tree->appendTo(QStringLiteral("showInsertMenu"), MenuNode::separator());
    for (const MenuNode &node : m_media->insertMenuItems()) {
        tree->appendTo(QStringLiteral("showInsertMenu"), node);
    }
    for (const MenuNode &node : m_media->playbackMenuItems()) {
        tree->appendTo(QStringLiteral("showShapeMenu"), node);
    }
    tree->appendTo(QStringLiteral("viewMenu"), MenuNode::separator());
    for (const MenuNode &node : m_masterView->menuItems()) {
        tree->appendTo(QStringLiteral("viewMenu"), node);
    }
}

ShowModel::Slide *ShowHost::currentSlide() const
{
    ShowModel::Presentation *deck = m_document->presentation();
    return m_currentRow >= 0 && m_currentRow < deck->slides.size() ? &deck->slides[m_currentRow] : nullptr;
}

bool ShowHost::masterViewActive() const
{
    return m_masterView != nullptr && m_masterView->active();
}

bool ShowHost::applyDeckChange(const ShowModel::Presentation &after, const QString &label)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (after == *deck) {
        return false;
    }
    m_canvas->commitTextEdit(false);
    if (!masterViewActive()) {
        m_canvas->clearSelection();
    }
    // AGENT-GUARD (show_commands.h): the command swaps the whole deck; the
    // canvas is re-seated by fullRefresh() on the stack's indexChanged.
    m_document->undoStack()->push(new DeckStateCommand(deck, *deck, after, label));
    m_document->setDirty(true);
    m_document->notifyContentChanged();
    fullRefresh();
    return true;
}

// Slide-list changes keep their sections (show_model's sections.h): a new
// slide joins the section of the slide before it, a deleted one leaves.
void ShowHost::pushSlideList(const ShowModel::Presentation &before, const QString &label)
{
    ShowModel::Presentation *deck = m_document->presentation();
    ShowModel::normalizeSections(*deck);
    m_document->undoStack()->push(new DeckStateCommand(deck, before, *deck, label));
}

} // namespace QindaOffice::Apps::Show
