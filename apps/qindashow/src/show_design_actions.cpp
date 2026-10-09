// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_design_actions.h"

#include "menu_tree.h"
#include "show_commands.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"
#include "standard_actions.h"

#include <QUndoStack>
#include <QUuid>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

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
    {ShowModel::ShapeAnimation::FlyInBottom, "showAnimFlyBottom", QT_TR_NOOP("Fly in from &bottom")},
    {ShowModel::ShapeAnimation::Zoom, "showAnimZoom", QT_TR_NOOP("&Zoom in")},
    // M67 (D-076): PowerPoint's Wipe and Split entrances, Spin and
    // Grow/Shrink emphasis.
    {ShowModel::ShapeAnimation::Wipe, "showAnimWipe", QT_TR_NOOP("&Wipe in")},
    {ShowModel::ShapeAnimation::Split, "showAnimSplit", QT_TR_NOOP("S&plit in")},
    {ShowModel::ShapeAnimation::Spin, "showAnimSpin", QT_TR_NOOP("Spi&n (emphasis)")},
    {ShowModel::ShapeAnimation::GrowShrink, "showAnimGrowShrink", QT_TR_NOOP("&Grow/Shrink (emphasis)")},
};

MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

} // namespace

ShowDesignActions::ShowDesignActions(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, [this] { syncFromSelection(); });
    // The toggle follows the canvas, not only the click that flipped it:
    // snapping is also set by preferences and by the canvas itself.
    connect(host->canvas(), &SlideCanvasItem::snapChanged, this, [this] {
        m_syncing = true;
        m_snap->setChecked(m_host->canvas()->snapEnabled());
        m_syncing = false;
    });
    syncFromSelection();
}

void ShowDesignActions::createActions()
{
    using namespace Quick::ActionId;
    Quick::ActionModel *model = m_host->actions();
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    m_group = make(kShowGroup, tr("&Group"), QKeySequence(QStringLiteral("Ctrl+G")), "group");
    m_ungroup = make(kShowUngroup, tr("&Ungroup"), QKeySequence(QStringLiteral("Ctrl+Shift+G")), "ungroup");
    m_snap = make(kShowSnapToGuides, tr("&Snap to Guides"));
    m_snap->setCheckable(true);
    m_snap->setChecked(true);
    m_masters = make(kShowMasters, tr("Slide &Masters…"));
    m_applyMaster = make(kShowApplyMaster, tr("Apply Master to Slide…"));

    SlideCanvasItem *canvas = m_host->canvas();
    connect(m_group, &ActionEntry::triggered, canvas, &SlideCanvasItem::groupSelected);
    connect(m_ungroup, &ActionEntry::triggered, canvas, &SlideCanvasItem::ungroupSelected);
    connect(m_snap, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_host->canvas()->setSnapEnabled(on);
        }
    });
    connect(m_masters, &ActionEntry::triggered, this, &ShowDesignActions::mastersRequested);
    connect(m_applyMaster, &ActionEntry::triggered, this, [this] {
        // Without any master there is nothing to choose: open the editor.
        if (m_host->showDocument()->presentation()->masters.isEmpty()) {
            emit mastersRequested();
        } else {
            emit applyMasterRequested();
        }
    });
    for (const AnimationEntry &entry : kAnimations) {
        ActionEntry *action = make(entry.id, tr(entry.label));
        action->setCheckable(true);
        action->setRadioGroup(QStringLiteral("showAnimation"));
        const ShowModel::ShapeAnimation animation = entry.animation;
        connect(action, &ActionEntry::triggered, this, [this, animation] {
            if (!m_syncing) {
                applyAnimation(animation);
            }
        });
        m_animations.append(action);
    }
}

void ShowDesignActions::asOneSlideEdit(const QString &text, const std::function<void()> &mutate)
{
    ShowModel::Slide *slide = m_host->canvas()->slide();
    if (slide == nullptr) {
        return;
    }
    const ShowModel::Slide before = *slide;
    mutate();
    if (*slide == before) {
        return;
    }
    auto *command = new SlideStateCommand(m_host->showDocument()->presentation(), slide->id, before, *slide);
    command->setText(text);
    m_host->showDocument()->undoStack()->push(command);
}

void ShowDesignActions::applyAnimation(ShowModel::ShapeAnimation animation)
{
    SlideCanvasItem *canvas = m_host->canvas();
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
            // Emphasis plays longer than an entrance (PowerPoint: 2 s, 0.5 s).
            if (ShowModel::isEmphasis(animation) != ShowModel::isEmphasis(shape.animation)) {
                shape.animationMs = ShowModel::isEmphasis(animation) ? 2000 : 500;
            }
            shape.animation = animation;
            // An animation only plays when the shape ARRIVES (or, for an
            // emphasis, at its step), so a shape that is on the slide from
            // the start gets a build step too — otherwise the setting looks
            // like it did nothing.
            if (animation != ShowModel::ShapeAnimation::Appear && shape.buildStep == 0) {
                shape.buildStep = 1;
            }
        }
    });
    syncFromSelection();
}

QVariantList ShowDesignActions::mastersAsVariant() const
{
    QVariantList list;
    for (const ShowModel::SlideMaster &master : m_host->showDocument()->presentation()->masters) {
        list.append(QVariantMap{{QStringLiteral("id"), master.id},
                                {QStringLiteral("name"), master.name},
                                {QStringLiteral("background"), master.background},
                                {QStringLiteral("titleColor"), master.titleColor},
                                {QStringLiteral("bodyColor"), master.bodyColor},
                                {QStringLiteral("fontFamily"), master.fontFamily}});
    }
    return list;
}

QStringList ShowDesignActions::masterNames() const
{
    QStringList names;
    for (const ShowModel::SlideMaster &master : m_host->showDocument()->presentation()->masters) {
        names.append(master.name.isEmpty() ? master.id : master.name);
    }
    return names;
}

void ShowDesignActions::applyMasters(const QVariantList &masters)
{
    ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    QList<ShowModel::SlideMaster> after;
    for (const QVariant &value : masters) {
        const QVariantMap map = value.toMap();
        ShowModel::SlideMaster master;
        master.id = map.value(QStringLiteral("id")).toString();
        if (master.id.isEmpty()) {
            master.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        }
        master.name = map.value(QStringLiteral("name")).toString();
        master.background = map.value(QStringLiteral("background")).value<QColor>();
        master.titleColor = map.value(QStringLiteral("titleColor")).value<QColor>();
        master.bodyColor = map.value(QStringLiteral("bodyColor")).value<QColor>();
        master.fontFamily = map.value(QStringLiteral("fontFamily")).toString();
        after.append(master);
    }
    if (deck->masters == after) {
        return;
    }
    // Masters are deck state, not slide state, so this is a whole-deck
    // command: restyling every slide at once is exactly the point.
    const QList<ShowModel::Slide> slidesBefore = deck->slides;
    deck->masters = after;
    auto *command = new SlideListCommand(deck, slidesBefore, deck->slides);
    command->setText(tr("Slide masters"));
    m_host->showDocument()->undoStack()->push(command);
    m_host->showDocument()->setDirty(true);
    m_host->showDocument()->notifyContentChanged();
    m_host->selectSlideRow(m_host->currentSlideRow());
}

void ShowDesignActions::applyMasterToSlide(int masterIndex)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const QString masterId = masterIndex >= 0 && masterIndex < deck->masters.size()
        ? deck->masters.at(masterIndex).id
        : QString();
    asOneSlideEdit(tr("Slide master"), [this, masterId] { m_host->canvas()->slide()->master = masterId; });
    m_host->selectSlideRow(m_host->currentSlideRow());
}

void ShowDesignActions::syncFromSelection()
{
    m_syncing = true;
    SlideCanvasItem *canvas = m_host->canvas();
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

QList<MenuNode> ShowDesignActions::shapeMenuItems() const
{
    QList<MenuNode> animations;
    for (const ActionEntry *entry : m_animations) {
        animations.append(a(entry));
    }
    return {MenuNode::separator(), a(m_group), a(m_ungroup),
            MenuNode::submenu(QStringLiteral("showAnimationMenu"), tr("A&nimation"), animations)};
}

QList<MenuNode> ShowDesignActions::slideMenuItems() const
{
    return {a(m_applyMaster), a(m_masters)};
}

QList<MenuNode> ShowDesignActions::viewMenuItems() const
{
    return {a(m_snap)};
}

QList<MenuNode> ShowDesignActions::contextMenuItems() const
{
    return {MenuNode::separator(), a(m_group), a(m_ungroup)};
}

QStringList ShowDesignActions::stripIds() const
{
    return {m_group->id(), m_ungroup->id()};
}

} // namespace QindaOffice::Apps::Show
