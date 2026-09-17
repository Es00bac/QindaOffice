// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_design_actions.h"

#include "show_commands.h"
#include "show_document.h"
#include "show_master_dialog.h"
#include "show_window.h"
#include "slide_canvas.h"

#include "standard_actions.h"

#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QUndoStack>

namespace QindaOffice::Apps::Show {

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

struct AnimationEntry {
    ShowModel::ShapeAnimation animation;
    const char *id;
    const char *label;
};

const AnimationEntry kAnimations[] = {
    {ShowModel::ShapeAnimation::Appear, "showAnimAppear", QT_TR_NOOP("&Appear")},
    {ShowModel::ShapeAnimation::Fade, "showAnimFade", QT_TR_NOOP("&Fade in")},
    {ShowModel::ShapeAnimation::FlyInLeft, "showAnimFlyLeft", QT_TR_NOOP("Fly in from &left")},
    {ShowModel::ShapeAnimation::FlyInRight, "showAnimFlyRight", QT_TR_NOOP("Fly in from &right")},
    {ShowModel::ShapeAnimation::FlyInTop, "showAnimFlyTop", QT_TR_NOOP("Fly in from &top")},
    {ShowModel::ShapeAnimation::FlyInBottom, "showAnimFlyBottom",
     QT_TR_NOOP("Fly in from &bottom")},
    {ShowModel::ShapeAnimation::Zoom, "showAnimZoom", QT_TR_NOOP("&Zoom in")},
};

} // namespace

ShowDesignActions::ShowDesignActions(ShowWindow *window)
    : QObject(window)
    , m_window(window)
{
    createActions();
    connect(window->canvas(), &SlideCanvas::selectionChanged, this,
            [this](bool) { syncFromSelection(); });
    syncFromSelection();
}

void ShowDesignActions::createActions()
{
    using namespace Shell::ActionId;
    m_group = make(this, kShowGroup, tr("&Group"), QKeySequence(QStringLiteral("Ctrl+G")),
                   "object-group");
    m_ungroup = make(this, kShowUngroup, tr("&Ungroup"),
                     QKeySequence(QStringLiteral("Ctrl+Shift+G")), "object-ungroup");
    m_snap = make(this, kShowSnapToGuides, tr("&Snap to Guides"));
    m_snap->setCheckable(true);
    m_snap->setChecked(true);
    m_masters = make(this, kShowMasters, tr("Slide &Masters…"));
    m_applyMaster = make(this, kShowApplyMaster, tr("Apply Master to Slide…"));

    connect(m_group, &QAction::triggered, m_window->canvas(), &SlideCanvas::groupSelected);
    connect(m_ungroup, &QAction::triggered, m_window->canvas(), &SlideCanvas::ungroupSelected);
    connect(m_snap, &QAction::triggered, this, [this](bool on) {
        m_window->canvas()->setSnapEnabled(on);
    });
    connect(m_masters, &QAction::triggered, this, &ShowDesignActions::editMasters);
    connect(m_applyMaster, &QAction::triggered, this, &ShowDesignActions::applyMasterToSlide);

    for (const AnimationEntry &entry : kAnimations) {
        QAction *action = make(this, entry.id, tr(entry.label));
        action->setCheckable(true);
        const ShowModel::ShapeAnimation animation = entry.animation;
        connect(action, &QAction::triggered, this, [this, animation] {
            if (!m_syncing) {
                applyAnimation(animation);
            }
        });
        m_animations.append(action);
    }
}

void ShowDesignActions::asOneSlideEdit(const QString &text, const std::function<void()> &mutate)
{
    SlideCanvas *canvas = m_window->canvas();
    ShowModel::Slide *slide = canvas->slide();
    if (slide == nullptr) {
        return;
    }
    const ShowModel::Slide before = *slide;
    mutate();
    if (*slide == before) {
        return;
    }
    auto *command = new SlideStateCommand(m_window->showDocument()->presentation(), slide->id,
                                          before, *slide);
    command->setText(text);
    m_window->showDocument()->undoStack()->push(command);
}

void ShowDesignActions::applyAnimation(ShowModel::ShapeAnimation animation)
{
    SlideCanvas *canvas = m_window->canvas();
    ShowModel::Slide *slide = canvas->slide();
    const QStringList ids = canvas->selectedShapeIds();
    if (slide == nullptr || ids.isEmpty()) {
        return;
    }
    asOneSlideEdit(tr("Animation"), [slide, ids, animation] {
        for (ShowModel::Shape &shape : slide->shapes) {
            if (!ids.contains(shape.id)) {
                continue;
            }
            shape.animation = animation;
            // An animation only plays when the shape ARRIVES, so a shape
            // that is on the slide from the start gets a build step too —
            // otherwise the setting looks like it did nothing.
            if (animation != ShowModel::ShapeAnimation::Appear && shape.buildStep == 0) {
                shape.buildStep = 1;
            }
        }
    });
    syncFromSelection();
}

void ShowDesignActions::editMasters()
{
    ShowModel::Presentation *deck = m_window->showDocument()->presentation();
    ShowMasterDialog dialog(deck->masters, m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QList<ShowModel::SlideMaster> before = deck->masters;
    const QList<ShowModel::SlideMaster> after = dialog.masters();
    if (before == after) {
        return;
    }
    // Masters are deck state, not slide state, so this is a whole-deck
    // command: restyling every slide at once is exactly the point.
    const QList<ShowModel::Slide> slidesBefore = deck->slides;
    deck->masters = after;
    auto *command = new SlideListCommand(deck, slidesBefore, deck->slides);
    command->setText(tr("Slide masters"));
    m_window->showDocument()->undoStack()->push(command);
    m_window->showDocument()->setDirty(true);
    m_window->showDocument()->notifyContentChanged();
    m_window->selectSlideRow(m_window->currentSlideRow());
}

void ShowDesignActions::applyMasterToSlide()
{
    ShowModel::Presentation *deck = m_window->showDocument()->presentation();
    if (deck->masters.isEmpty()) {
        editMasters();
        if (deck->masters.isEmpty()) {
            return;
        }
    }
    QStringList names{tr("None (plain white)")};
    for (const ShowModel::SlideMaster &master : deck->masters) {
        names.append(master.name.isEmpty() ? master.id : master.name);
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(m_window, tr("Apply Master"), tr("Master:"),
                                                 names, 0, false, &ok);
    if (!ok) {
        return;
    }
    const int index = names.indexOf(chosen) - 1;
    const QString masterId = index >= 0 ? deck->masters.at(index).id : QString();
    asOneSlideEdit(tr("Slide master"), [this, masterId] {
        m_window->canvas()->slide()->master = masterId;
    });
    m_window->selectSlideRow(m_window->currentSlideRow());
}

void ShowDesignActions::syncFromSelection()
{
    m_syncing = true;
    SlideCanvas *canvas = m_window->canvas();
    const QStringList ids = canvas->slide() != nullptr ? canvas->selectedShapeIds() : QStringList();
    m_group->setEnabled(ids.size() >= 2);
    m_ungroup->setEnabled(canvas->selectionIsGrouped());
    m_snap->setChecked(canvas->snapEnabled());
    // The animation entries show what the selection already has; a mixed
    // selection shows nothing checked rather than a guess.
    ShowModel::ShapeAnimation shared = ShowModel::ShapeAnimation::Appear;
    bool uniform = !ids.isEmpty();
    bool first = true;
    for (const QString &id : ids) {
        const ShowModel::Shape *shape = canvas->slide()->findShape(id);
        if (shape == nullptr) {
            continue;
        }
        if (first) {
            shared = shape->animation;
            first = false;
        } else if (shape->animation != shared) {
            uniform = false;
        }
    }
    for (int i = 0; i < m_animations.size(); ++i) {
        m_animations.at(i)->setEnabled(!ids.isEmpty());
        m_animations.at(i)->setChecked(uniform && kAnimations[i].animation == shared);
    }
    m_syncing = false;
}

void ShowDesignActions::populateShapeMenu(QMenu *menu)
{
    menu->addSeparator();
    menu->addAction(m_group);
    menu->addAction(m_ungroup);
    auto *animations = menu->addMenu(tr("A&nimation"));
    animations->setObjectName(QStringLiteral("showAnimationMenu"));
    for (QAction *action : m_animations) {
        animations->addAction(action);
    }
}

void ShowDesignActions::populateSlideMenu(QMenu *menu)
{
    menu->addAction(m_applyMaster);
    menu->addAction(m_masters);
}

void ShowDesignActions::populateViewMenu(QMenu *menu)
{
    menu->addAction(m_snap);
}

void ShowDesignActions::populateContextMenu(QMenu *menu)
{
    menu->addSeparator();
    menu->addAction(m_group);
    menu->addAction(m_ungroup);
}

QList<QAction *> ShowDesignActions::stripActions() const
{
    return {m_group, m_ungroup};
}

} // namespace QindaOffice::Apps::Show
