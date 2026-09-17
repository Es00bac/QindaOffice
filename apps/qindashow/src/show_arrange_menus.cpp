// SPDX-License-Identifier: GPL-3.0-or-later
// The menus QindaShow's arrange commands are reached through. Split from
// show_arrange.cpp to keep both files inside the line budget; every entry
// reuses the persistent action, so a command's enabled state is identical
// in the menu bar, the toolbar and the canvas context menu.

#include "show_arrange.h"

#include "standard_actions.h"

#include <QAction>
#include <QMenu>

namespace QindaOffice::Apps::Show {

void ShowArrangeActions::populateArrangeMenu(QMenu *menu)
{
    menu->addAction(m_bringToFront);
    menu->addAction(m_bringForward);
    menu->addAction(m_sendBackward);
    menu->addAction(m_sendToBack);
    menu->addSeparator();
    auto *align = menu->addMenu(tr("&Align"));
    align->setObjectName(QStringLiteral("showAlignMenu"));
    align->addAction(m_alignLeft);
    align->addAction(m_alignHCenter);
    align->addAction(m_alignRight);
    align->addSeparator();
    align->addAction(m_alignTop);
    align->addAction(m_alignVCenter);
    align->addAction(m_alignBottom);
    align->addSeparator();
    align->addAction(m_distributeH);
    align->addAction(m_distributeV);
    menu->addSeparator();
    menu->addAction(m_duplicate);
    menu->addAction(m_deleteShape);
}

void ShowArrangeActions::populateSlideMenu(QMenu *menu)
{
    menu->addAction(m_slideMoveUp);
    menu->addAction(m_slideMoveDown);
}

QList<QAction *> ShowArrangeActions::stripActions() const
{
    return {m_bringToFront, m_sendToBack, m_alignLeft, m_alignHCenter,
            m_alignRight,   m_alignTop,   m_alignVCenter, m_alignBottom};
}

void ShowArrangeActions::populateContextMenu(QMenu *menu)
{
    menu->addAction(m_duplicate);
    menu->addAction(m_deleteShape);
    menu->addSeparator();
    menu->addAction(m_bringToFront);
    menu->addAction(m_bringForward);
    menu->addAction(m_sendBackward);
    menu->addAction(m_sendToBack);
}

} // namespace QindaOffice::Apps::Show
