// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's arrange commands: z-order (front/forward/backward/back),
// duplicate and delete, align and distribute, and moving the current slide
// up or down the deck.
//
// Rationale (D-016). The Arrange menu held exactly two entries — "Bring
// forward" and "Send backward" — built as anonymous lambdas with no
// objectName, so they were unreachable from the panel's global menu
// (AGENTS.md rule 5); there was no way to line shapes up with each other or
// with the slide, no way to duplicate a shape, and the only way to reorder
// slides was to drag a thumbnail.
//
// Geometry changes for several shapes at once are pushed as ONE
// SlideStateCommand: aligning six shapes is one action to the user and must
// be one Ctrl+Z.

#include <QAction>
#include <QObject>

#include <functional>

class QMenu;

namespace QindaOffice::Apps::Show {

class ShowWindow;

class ShowArrangeActions final : public QObject {
    Q_OBJECT
public:
    explicit ShowArrangeActions(ShowWindow *window);

    void populateArrangeMenu(QMenu *menu);
    void populateSlideMenu(QMenu *menu);
    [[nodiscard]] QList<QAction *> stripActions() const;
    // Shown on a right-click over the canvas.
    void populateContextMenu(QMenu *menu);

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
    // Runs `mutate` between a before/after snapshot of the current slide and
    // pushes the pair as one undo command.
    void asOneSlideEdit(const QString &text, const std::function<void()> &mutate);

    ShowWindow *m_window;
    QAction *m_bringToFront = nullptr;
    QAction *m_bringForward = nullptr;
    QAction *m_sendBackward = nullptr;
    QAction *m_sendToBack = nullptr;
    QAction *m_duplicate = nullptr;
    QAction *m_deleteShape = nullptr;
    QAction *m_alignLeft = nullptr;
    QAction *m_alignHCenter = nullptr;
    QAction *m_alignRight = nullptr;
    QAction *m_alignTop = nullptr;
    QAction *m_alignVCenter = nullptr;
    QAction *m_alignBottom = nullptr;
    QAction *m_distributeH = nullptr;
    QAction *m_distributeV = nullptr;
    QAction *m_slideMoveUp = nullptr;
    QAction *m_slideMoveDown = nullptr;
};

} // namespace QindaOffice::Apps::Show
