// SPDX-License-Identifier: GPL-3.0-or-later
// The Slide Master view's commands and menu (show_master_view.h): View ▸
// Slide Master, and the Master Tools — Insert / Duplicate / Delete /
// Rename / Preserve Layout, Title, Footers, Insert Placeholder, Text
// Styles, Close Master View.

#include "show_master_view.h"

#include "action_model.h"
#include "menu_tree.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace MasterId = MasterActionId;

namespace {

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

void ShowMasterView::createActions()
{
    Quick::ActionModel *model = m_host->actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        m_actions.insert(QString::fromLatin1(id), entry);
        return entry;
    };
    ActionEntry *view = make(MasterId::kView, tr("Slide &Master"), "layout-template");
    view->setCheckable(true);
    connect(view, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            on ? enter() : close();
        }
    });
    connect(make(MasterId::kClose, tr("&Close Master View"), "circle-x"), &ActionEntry::triggered, this,
            &ShowMasterView::close);
    connect(make(MasterId::kInsertLayout, tr("Insert &Layout"), "panels-top-left"), &ActionEntry::triggered, this, [this] {
        QString added;
        const QString master = m_masterId;
        const QString after = m_layoutId;
        if (change(tr("Insert layout"), [&](ShowModel::Presentation &deck) {
                added = ShowModel::insertLayout(deck, master, after);
                return !added.isEmpty();
            })) {
            select(master, added);
        }
    });
    connect(make(MasterId::kDuplicateLayout, tr("&Duplicate Layout"), "copy"), &ActionEntry::triggered, this, [this] {
        QString added;
        const QString layout = m_layoutId;
        if (change(tr("Duplicate layout"), [&](ShowModel::Presentation &deck) {
                added = ShowModel::duplicateLayout(deck, layout);
                return !added.isEmpty();
            })) {
            select(m_masterId, added);
        }
    });
    connect(make(MasterId::kDeleteLayout, tr("D&elete Layout"), "trash-2"), &ActionEntry::triggered, this, [this] {
        const QString layout = m_layoutId;
        if (ShowModel::slidesUsingLayout(*m_host->showDocument()->presentation(), layout) > 0) {
            m_host->showStatusMessage(tr("Slides use this layout: it cannot be deleted"), 6000);
            return;
        }
        const int index = currentIndex();
        if (change(tr("Delete layout"), [&](ShowModel::Presentation &deck) { return ShowModel::deleteLayout(deck, layout); })) {
            const QVariantList all = items();
            const QVariantMap next = all.value(qMin(index, int(all.size()) - 1)).toMap();
            select(next.value(QStringLiteral("masterId")).toString(), next.value(QStringLiteral("layoutId")).toString());
        }
    });
    connect(make(MasterId::kRename, tr("&Rename…"), "pencil"), &ActionEntry::triggered, this,
            [this] { emit renameRequested(currentName()); });
    ActionEntry *preserve = make(MasterId::kPreserve, tr("&Preserve"), "pin");
    preserve->setCheckable(true);
    connect(preserve, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        const QString layout = m_layoutId;
        change(tr("Preserve layout"), [&](ShowModel::Presentation &deck) { return ShowModel::setLayoutPreserved(deck, layout, on); });
    });
    ActionEntry *title = make(MasterId::kTitle, tr("&Title"), "heading");
    title->setCheckable(true);
    connect(title, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        const QString layout = m_layoutId;
        change(tr("Layout title"), [&](ShowModel::Presentation &deck) { return ShowModel::setLayoutTitle(deck, layout, on); });
    });
    ActionEntry *footers = make(MasterId::kFooters, tr("&Footers"), "panel-bottom");
    footers->setCheckable(true);
    connect(footers, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        const QString layout = m_layoutId;
        change(tr("Layout footers"), [&](ShowModel::Presentation &deck) { return ShowModel::setLayoutFooters(deck, layout, on); });
    });
    const auto placeholder = [this, make](const char *id, const QString &text, const char *icon, const QString &type) {
        connect(make(id, text, icon), &ActionEntry::triggered, this, [this, type] {
            const QSizeF size = m_host->showDocument()->presentation()->slideSize;
            const QRectF rect(size.width() * 0.25, size.height() * 0.3, size.width() * 0.5, size.height() * 0.4);
            const QString layout = m_layoutId;
            change(tr("Insert placeholder"), [&](ShowModel::Presentation &deck) {
                return !ShowModel::insertPlaceholder(deck, layout, type, rect).isEmpty();
            });
        });
    };
    placeholder(MasterId::kContentPlaceholder, tr("&Content Placeholder"), "layout-grid", QStringLiteral("obj"));
    placeholder(MasterId::kTextPlaceholder, tr("Te&xt Placeholder"), "text", QStringLiteral("body"));
    placeholder(MasterId::kPicturePlaceholder, tr("&Picture Placeholder"), "image", QStringLiteral("pic"));
    ActionEntry *styles = make(MasterId::kTextStyles, tr("Text &Styles"), "type");
    styles->setCheckable(true);
    connect(styles, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            emit textStylesRequested(on);
        }
    });
    syncActions();
}

void ShowMasterView::syncActions()
{
    m_syncing = true;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::SlideLayout *layout = m_active ? deck->findLayout(m_layoutId) : nullptr;
    m_actions.value(QString::fromLatin1(MasterId::kView))->setChecked(m_active);
    for (const char *id : {MasterId::kClose, MasterId::kInsertLayout, MasterId::kRename, MasterId::kTextStyles}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(m_active);
    }
    for (const char *id : {MasterId::kDuplicateLayout, MasterId::kPreserve, MasterId::kTitle, MasterId::kFooters,
                           MasterId::kContentPlaceholder, MasterId::kTextPlaceholder, MasterId::kPicturePlaceholder}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(layout != nullptr);
    }
    // PowerPoint keeps a layout slides use: Delete is off for it.
    m_actions.value(QString::fromLatin1(MasterId::kDeleteLayout))
        ->setEnabled(layout != nullptr && ShowModel::slidesUsingLayout(*deck, layout->id) == 0);
    m_actions.value(QString::fromLatin1(MasterId::kPreserve))->setChecked(layout != nullptr && layout->preserve);
    m_actions.value(QString::fromLatin1(MasterId::kTitle))->setChecked(layout != nullptr && ShowModel::layoutHasTitle(*layout));
    m_actions.value(QString::fromLatin1(MasterId::kFooters))->setChecked(layout != nullptr && ShowModel::layoutHasFooters(*layout));
    m_syncing = false;
}

QList<MenuNode> ShowMasterView::menuItems() const
{
    return {a(MasterId::kView),
            MenuNode::submenu(QStringLiteral("showMasterToolsMenu"), tr("Master &Tools"),
                              {a(MasterId::kInsertLayout), a(MasterId::kDuplicateLayout), a(MasterId::kDeleteLayout),
                               a(MasterId::kRename), a(MasterId::kPreserve), MenuNode::separator(), a(MasterId::kTitle),
                               a(MasterId::kFooters),
                               MenuNode::submenu(QStringLiteral("showInsertPlaceholderMenu"), tr("Insert &Placeholder"),
                                                 {a(MasterId::kContentPlaceholder), a(MasterId::kTextPlaceholder),
                                                  a(MasterId::kPicturePlaceholder)}),
                               MenuNode::separator(), a(MasterId::kTextStyles), a(MasterId::kClose)})};
}

} // namespace QindaOffice::Apps::Show
