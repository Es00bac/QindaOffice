// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Animations ▸ Effect Options and exit effects in QindaShow (M91, D-099),
// as PowerPoint has them for the selected shapes:
//  - Effect Options: the entrance's direction — Fly In from any of eight
//    edges and corners, Wipe and Peek In from four edges, Split in or out
//    horizontally or vertically, Float In up or down (show_model's
//    Shape::animationDirection, PowerPoint's presetSubtype);
//  - Exit: none, Disappear, Fade, Fly Out, Wipe, Split, Peek Out, Float
//    Out, Zoom — at the click after the slide's last one (Shape::exit);
//  - Exit Options: the exit's direction, as Effect Options does for the
//    entrance.
// Each is ONE undo step over the selection; an option the selected effect
// does not have is off. The show plays them in that direction.
//
// AGENT-CONTRACT: the command ids are the dbusmenu activation identity
// (AGENTS.md rule 5): never renamed, never reused.

#include "action_model.h"
#include "presentation.h"

#include <QObject>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

class ShowEffectOptions final : public QObject {
    Q_OBJECT
public:
    explicit ShowEffectOptions(ShowHost *host);

    // Animation ▸ Effect Options, Exit, Exit Options (submenus).
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    // The ribbon's three menus' ids.
    [[nodiscard]] QStringList directionIds() const;
    [[nodiscard]] QStringList exitIds() const;
    [[nodiscard]] QStringList exitDirectionIds() const;
    void sync();

    // The directions the effect `animation` offers (presetSubtype values).
    [[nodiscard]] static QList<int> directionsOf(ShowModel::ShapeAnimation animation);

private:
    void setDirection(int direction, bool exit);
    void setExit(int kind);
    bool editSelection(const QString &label, const std::function<void(ShowModel::Shape &, int nextStep)> &mutate);

    ShowHost *m_host;
    QList<std::pair<int, Quick::ActionEntry *>> m_directions;     // direction → entry
    QList<std::pair<int, Quick::ActionEntry *>> m_exitDirections;
    QList<Quick::ActionEntry *> m_exits;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
