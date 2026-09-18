// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's design commands: slide masters, per-shape build animations,
// grouping, and the snapping toggle. All four are model state, so all four
// survive save/reload through the fodp writer's qq: namespace and are
// undoable like any other edit. The master editor and the master chooser
// are request signals the window answers with applyMasters() /
// applyMasterToSlide().

#include "action_model.h"
#include "presentation.h"

#include <QObject>
#include <QVariantList>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

class ShowDesignActions final : public QObject {
    Q_OBJECT
public:
    explicit ShowDesignActions(ShowHost *host);

    [[nodiscard]] QList<Quick::MenuNode> shapeMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> slideMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> viewMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> contextMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;
    void syncFromSelection();

    // Masters as maps {id, name, background, titleColor, bodyColor, fontFamily}.
    [[nodiscard]] QVariantList mastersAsVariant() const;
    [[nodiscard]] QStringList masterNames() const;
    // The dialogs' answers: the whole master list, one undo step; the
    // chosen master (index into the deck's list, -1 = plain white).
    void applyMasters(const QVariantList &masters);
    void applyMasterToSlide(int masterIndex);

signals:
    void mastersRequested();
    // Sent when the deck has masters to choose from; without any the
    // editor opens instead.
    void applyMasterRequested();

private:
    void createActions();
    void applyAnimation(ShowModel::ShapeAnimation animation);
    void asOneSlideEdit(const QString &text, const std::function<void()> &mutate);

    ShowHost *m_host;
    Quick::ActionEntry *m_group = nullptr;
    Quick::ActionEntry *m_ungroup = nullptr;
    Quick::ActionEntry *m_snap = nullptr;
    Quick::ActionEntry *m_masters = nullptr;
    Quick::ActionEntry *m_applyMaster = nullptr;
    QList<Quick::ActionEntry *> m_animations;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
