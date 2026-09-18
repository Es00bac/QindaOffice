// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's arrange commands (D-016): z-order (front/forward/backward/
// back), duplicate and delete, align and distribute, and moving the
// current slide up or down the deck — persistent entries (rule 5) reached
// from the Arrange menu, the strip and the canvas context menu alike.
// Geometry changes for several shapes at once are pushed as ONE
// SlideStateCommand: aligning six shapes is one action to the user.

#include "action_model.h"

#include <QObject>

#include <functional>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

class ShowArrangeActions final : public QObject {
    Q_OBJECT
public:
    explicit ShowArrangeActions(ShowHost *host);

    [[nodiscard]] QList<Quick::MenuNode> arrangeMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> slideMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> contextMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;

    // Re-reads the selection so the commands that need one are enabled and
    // the ones that need several (distribute) only light up when they can
    // do something.
    void syncFromSelection();

private:
    enum class Align { Left, HCenter, Right, Top, VCenter, Bottom };

    void createActions();
    void applyAlign(Align align);
    void distribute(bool horizontal);
    void duplicateSelected();
    void moveSlide(int delta);
    void asOneSlideEdit(const QString &text, const std::function<void()> &mutate);

    ShowHost *m_host;
    Quick::ActionEntry *m_bringToFront = nullptr;
    Quick::ActionEntry *m_bringForward = nullptr;
    Quick::ActionEntry *m_sendBackward = nullptr;
    Quick::ActionEntry *m_sendToBack = nullptr;
    Quick::ActionEntry *m_duplicate = nullptr;
    Quick::ActionEntry *m_deleteShape = nullptr;
    Quick::ActionEntry *m_alignLeft = nullptr;
    Quick::ActionEntry *m_alignHCenter = nullptr;
    Quick::ActionEntry *m_alignRight = nullptr;
    Quick::ActionEntry *m_alignTop = nullptr;
    Quick::ActionEntry *m_alignVCenter = nullptr;
    Quick::ActionEntry *m_alignBottom = nullptr;
    Quick::ActionEntry *m_distributeH = nullptr;
    Quick::ActionEntry *m_distributeV = nullptr;
    Quick::ActionEntry *m_slideMoveUp = nullptr;
    Quick::ActionEntry *m_slideMoveDown = nullptr;
};

} // namespace QindaOffice::Apps::Show
