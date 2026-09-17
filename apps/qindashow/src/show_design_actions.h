// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's design commands: slide masters, per-shape build animations,
// grouping, and the snapping toggle.
//
// All four are model state, so all four survive save/reload through the
// fodp writer's qq: extension namespace and are undoable like any other
// edit. The renderer applies masters and animations, which is what keeps
// the editor, the thumbnails, the presenter view, the player and the PDF
// showing the same deck.

#include "presentation.h"

#include <QAction>
#include <QObject>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QMenu;

namespace QindaOffice::Apps::Show {

class ShowWindow;

class ShowDesignActions final : public QObject {
    Q_OBJECT
public:
    explicit ShowDesignActions(ShowWindow *window);

    void populateShapeMenu(QMenu *menu);
    void populateSlideMenu(QMenu *menu);
    void populateViewMenu(QMenu *menu);
    void populateContextMenu(QMenu *menu);
    [[nodiscard]] QList<QAction *> stripActions() const;

    void syncFromSelection();

private:
    void createActions();
    void applyAnimation(ShowModel::ShapeAnimation animation);
    void editMasters();
    void applyMasterToSlide();
    // Runs `mutate` between a before/after snapshot of the current slide and
    // pushes the pair as one undo command.
    void asOneSlideEdit(const QString &text, const std::function<void()> &mutate);

    ShowWindow *m_window;
    QAction *m_group = nullptr;
    QAction *m_ungroup = nullptr;
    QAction *m_snap = nullptr;
    QAction *m_masters = nullptr;
    QAction *m_applyMaster = nullptr;
    QList<QAction *> m_animations;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
