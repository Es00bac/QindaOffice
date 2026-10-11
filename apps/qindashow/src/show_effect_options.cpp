// SPDX-License-Identifier: GPL-3.0-or-later
// Effect Options and exit effects (show_effect_options.h).

#include "show_effect_options.h"

#include "menu_tree.h"
#include "show_commands.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QUndoStack>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace Direction = ShowModel::Direction;
using ShowModel::ShapeAnimation;

namespace {

struct DirectionEntry {
    int direction;
    const char *entrance;
    const char *exit;
    const char *entranceLabel;
    const char *exitLabel;
    bool floating; // Float's up / down
};

// AGENT-NOTE: Float's two entries carry the way it moves (show_model's
// effectiveDirection: Top = Float Up, Bottom = Float Down).
const DirectionEntry kDirections[] = {
    {Direction::Bottom, "showEffectFromBottomAction", "showExitToBottomAction", QT_TR_NOOP("From &Bottom"), QT_TR_NOOP("To &Bottom"), false},
    {Direction::Left, "showEffectFromLeftAction", "showExitToLeftAction", QT_TR_NOOP("From &Left"), QT_TR_NOOP("To &Left"), false},
    {Direction::Right, "showEffectFromRightAction", "showExitToRightAction", QT_TR_NOOP("From &Right"), QT_TR_NOOP("To &Right"), false},
    {Direction::Top, "showEffectFromTopAction", "showExitToTopAction", QT_TR_NOOP("From &Top"), QT_TR_NOOP("To &Top"), false},
    {Direction::BottomLeft, "showEffectFromBottomLeftAction", "showExitToBottomLeftAction", QT_TR_NOOP("From Bottom-Left"),
     QT_TR_NOOP("To Bottom-Left"), false},
    {Direction::BottomRight, "showEffectFromBottomRightAction", "showExitToBottomRightAction", QT_TR_NOOP("From Bottom-Right"),
     QT_TR_NOOP("To Bottom-Right"), false},
    {Direction::TopLeft, "showEffectFromTopLeftAction", "showExitToTopLeftAction", QT_TR_NOOP("From Top-Left"),
     QT_TR_NOOP("To Top-Left"), false},
    {Direction::TopRight, "showEffectFromTopRightAction", "showExitToTopRightAction", QT_TR_NOOP("From Top-Right"),
     QT_TR_NOOP("To Top-Right"), false},
    {Direction::HorizontalIn, "showEffectHorizontalInAction", "showExitHorizontalInAction", QT_TR_NOOP("&Horizontal In"),
     QT_TR_NOOP("&Horizontal In"), false},
    {Direction::HorizontalOut, "showEffectHorizontalOutAction", "showExitHorizontalOutAction", QT_TR_NOOP("Horizontal &Out"),
     QT_TR_NOOP("Horizontal &Out"), false},
    {Direction::VerticalIn, "showEffectVerticalInAction", "showExitVerticalInAction", QT_TR_NOOP("&Vertical In"),
     QT_TR_NOOP("&Vertical In"), false},
    {Direction::VerticalOut, "showEffectVerticalOutAction", "showExitVerticalOutAction", QT_TR_NOOP("Vertical O&ut"),
     QT_TR_NOOP("Vertical O&ut"), false},
    {Direction::Top, "showEffectFloatUpAction", "showExitFloatUpAction", QT_TR_NOOP("Float &Up"), QT_TR_NOOP("Float &Up"), true},
    {Direction::Bottom, "showEffectFloatDownAction", "showExitFloatDownAction", QT_TR_NOOP("Float &Down"), QT_TR_NOOP("Float &Down"),
     true},
};

struct ExitEntry {
    const char *id;
    const char *label;
    int kind; // ShapeAnimation, -1 = none
};
const ExitEntry kExits[] = {
    {"showExitNoneAction", QT_TR_NOOP("&None"), -1},
    {"showExitDisappearAction", QT_TR_NOOP("&Disappear"), int(ShapeAnimation::Appear)},
    {"showExitFadeAction", QT_TR_NOOP("&Fade Out"), int(ShapeAnimation::Fade)},
    {"showExitFlyOutAction", QT_TR_NOOP("Fly &Out"), int(ShapeAnimation::FlyInBottom)},
    {"showExitWipeAction", QT_TR_NOOP("&Wipe"), int(ShapeAnimation::Wipe)},
    {"showExitSplitAction", QT_TR_NOOP("S&plit"), int(ShapeAnimation::Split)},
    {"showExitPeekAction", QT_TR_NOOP("P&eek Out"), int(ShapeAnimation::Peek)},
    {"showExitFloatAction", QT_TR_NOOP("F&loat Out"), int(ShapeAnimation::Float)},
    {"showExitZoomAction", QT_TR_NOOP("&Zoom"), int(ShapeAnimation::Zoom)},
};

bool isFlyIn(ShapeAnimation animation)
{
    return animation == ShapeAnimation::FlyInLeft || animation == ShapeAnimation::FlyInRight || animation == ShapeAnimation::FlyInTop
        || animation == ShapeAnimation::FlyInBottom;
}

bool cardinal(int direction)
{
    return direction == Direction::Top || direction == Direction::Bottom || direction == Direction::Left || direction == Direction::Right;
}

// The kind and the stored direction for `direction` (Fly In's corners keep
// their edge's kind, as the pptx reader makes them: flyInFor()).
void place(ShapeAnimation &animation, int &stored, int direction)
{
    if (isFlyIn(animation)) {
        animation = ShowModel::flyInFor(direction);
        stored = cardinal(direction) ? 0 : direction;
        if (stored == 0 && ShowModel::effectiveDirection(animation, 0) != direction) {
            stored = direction;
        }
        return;
    }
    stored = direction == ShowModel::effectiveDirection(animation, 0) ? 0 : direction;
}

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

QList<int> ShowEffectOptions::directionsOf(ShapeAnimation animation)
{
    if (isFlyIn(animation)) {
        return {Direction::Bottom, Direction::Left, Direction::Right, Direction::Top,
                Direction::BottomLeft, Direction::BottomRight, Direction::TopLeft, Direction::TopRight};
    }
    switch (animation) {
    case ShapeAnimation::Wipe:
    case ShapeAnimation::Peek:
        return {Direction::Bottom, Direction::Left, Direction::Right, Direction::Top};
    case ShapeAnimation::Split:
        return {Direction::HorizontalIn, Direction::HorizontalOut, Direction::VerticalIn, Direction::VerticalOut};
    case ShapeAnimation::Float:
        return {Direction::Top, Direction::Bottom};
    default:
        return {};
    }
}

ShowEffectOptions::ShowEffectOptions(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    Quick::ActionModel *model = host->actions();
    for (const DirectionEntry &entry : kDirections) {
        for (const bool exit : {false, true}) {
            ActionEntry *action = model->add(QString::fromLatin1(exit ? entry.exit : entry.entrance), tr(exit ? entry.exitLabel : entry.entranceLabel));
            action->setCheckable(true);
            action->setRadioGroup(exit ? QStringLiteral("showExitDirection") : QStringLiteral("showEffectDirection"));
            const int direction = entry.direction;
            connect(action, &ActionEntry::triggered, this, [this, direction, exit] {
                if (!m_syncing) {
                    setDirection(direction, exit);
                }
            });
            (exit ? m_exitDirections : m_directions).append({direction, action});
        }
    }
    for (const ExitEntry &entry : kExits) {
        ActionEntry *action = model->add(QString::fromLatin1(entry.id), tr(entry.label));
        action->setCheckable(true);
        action->setRadioGroup(QStringLiteral("showExitEffect"));
        const int kind = entry.kind;
        connect(action, &ActionEntry::triggered, this, [this, kind] {
            if (!m_syncing) {
                setExit(kind);
            }
        });
        m_exits.append(action);
    }
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, &ShowEffectOptions::sync);
    connect(host, &ShowHost::deckChanged, this, &ShowEffectOptions::sync);
    sync();
}

QList<MenuNode> ShowEffectOptions::menuItems() const
{
    QList<MenuNode> directions;
    QList<MenuNode> exitDirections;
    for (const DirectionEntry &entry : kDirections) {
        directions.append(a(entry.entrance));
        exitDirections.append(a(entry.exit));
    }
    QList<MenuNode> exits;
    for (const ExitEntry &entry : kExits) {
        exits.append(a(entry.id));
    }
    return {MenuNode::submenu(QStringLiteral("showEffectOptionsMenu"), tr("Effect &Options"), directions),
            MenuNode::submenu(QStringLiteral("showExitMenu"), tr("E&xit"), exits),
            MenuNode::submenu(QStringLiteral("showExitOptionsMenu"), tr("Exit O&ptions"), exitDirections)};
}

QStringList ShowEffectOptions::directionIds() const
{
    QStringList ids;
    for (const auto &[direction, entry] : m_directions) {
        ids.append(entry->id());
    }
    return ids;
}

QStringList ShowEffectOptions::exitIds() const
{
    QStringList ids;
    for (const ActionEntry *entry : m_exits) {
        ids.append(entry->id());
    }
    return ids;
}

QStringList ShowEffectOptions::exitDirectionIds() const
{
    QStringList ids;
    for (const auto &[direction, entry] : m_exitDirections) {
        ids.append(entry->id());
    }
    return ids;
}

bool ShowEffectOptions::editSelection(const QString &label, const std::function<void(ShowModel::Shape &, int)> &mutate)
{
    SlideCanvasItem *canvas = m_host->canvas();
    ShowModel::Slide *slide = canvas->slide();
    const QStringList ids = canvas->selectedShapeIds();
    if (slide == nullptr || ids.isEmpty() || m_host->masterViewActive()) {
        return false;
    }
    int last = 0;
    for (const ShowModel::Shape &shape : slide->shapes) {
        last = qMax(last, qMax(shape.buildStep, shape.exit.step));
    }
    const ShowModel::Slide before = *slide;
    for (ShowModel::Shape &shape : slide->shapes) {
        if (ids.contains(shape.id)) {
            mutate(shape, last + 1);
        }
    }
    if (*slide == before) {
        return false;
    }
    auto *command = new SlideStateCommand(m_host->showDocument()->presentation(), slide->id, before, *slide);
    command->setText(label);
    m_host->showDocument()->undoStack()->push(command);
    sync();
    return true;
}

void ShowEffectOptions::setDirection(int direction, bool exit)
{
    editSelection(exit ? tr("Exit options") : tr("Effect options"), [direction, exit](ShowModel::Shape &shape, int) {
        if (exit) {
            if (shape.exit.isSet() && directionsOf(shape.exit.animation).contains(direction)) {
                place(shape.exit.animation, shape.exit.direction, direction);
            }
        } else if (directionsOf(shape.animation).contains(direction) && shape.buildStep > 0) {
            place(shape.animation, shape.animationDirection, direction);
        }
    });
}

void ShowEffectOptions::setExit(int kind)
{
    editSelection(tr("Exit effect"), [kind](ShowModel::Shape &shape, int next) {
        if (kind < 0) {
            shape.exit = ShowModel::ShapeExit();
            return;
        }
        const int step = shape.exit.isSet() ? shape.exit.step : qMax(next, shape.buildStep + 1);
        shape.exit = {step, ShapeAnimation(kind), 0, kind == int(ShapeAnimation::Float) ? 1000 : 500};
    });
}

void ShowEffectOptions::sync()
{
    m_syncing = true;
    SlideCanvasItem *canvas = m_host->canvas();
    const ShowModel::Shape *shape = canvas->selectionCount() >= 1 && !m_host->masterViewActive() ? canvas->selectedShape() : nullptr;
    const bool floating = shape != nullptr && shape->animation == ShapeAnimation::Float;
    for (qsizetype i = 0; i < m_directions.size(); ++i) {
        const auto &[direction, entry] = m_directions.at(i);
        const bool floatEntry = kDirections[i].floating;
        const bool offered = shape != nullptr && shape->buildStep > 0 && floatEntry == floating
            && directionsOf(shape->animation).contains(direction);
        entry->setEnabled(offered);
        entry->setChecked(offered && ShowModel::effectiveDirection(shape->animation, shape->animationDirection) == direction);
    }
    const bool exitFloating = shape != nullptr && shape->exit.animation == ShapeAnimation::Float;
    for (qsizetype i = 0; i < m_exitDirections.size(); ++i) {
        const auto &[direction, entry] = m_exitDirections.at(i);
        const bool offered = shape != nullptr && shape->exit.isSet() && kDirections[i].floating == exitFloating
            && directionsOf(shape->exit.animation).contains(direction);
        entry->setEnabled(offered);
        entry->setChecked(offered && ShowModel::effectiveDirection(shape->exit.animation, shape->exit.direction) == direction);
    }
    for (qsizetype i = 0; i < m_exits.size(); ++i) {
        ActionEntry *entry = m_exits.at(i);
        entry->setEnabled(shape != nullptr);
        int kind = shape != nullptr && shape->exit.isSet() ? int(shape->exit.animation) : -1;
        if (shape != nullptr && shape->exit.isSet() && isFlyIn(shape->exit.animation)) {
            kind = int(ShapeAnimation::FlyInBottom);
        }
        entry->setChecked(shape != nullptr && kExits[i].kind == kind);
    }
    m_syncing = false;
}

} // namespace QindaOffice::Apps::Show
