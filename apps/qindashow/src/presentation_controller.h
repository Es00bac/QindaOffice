// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PresentationController (plan section 7.5): the (slideIndex, buildStep)
// pair that drives the player and the presenter. Advancing reveals builds
// before slides; going back shows the previous slide fully revealed. Pure
// logic over a live Presentation so tests exercise it without widgets.

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include <QObject>

namespace QindaOffice::Apps::Show {

class PresentationController final : public QObject {
    Q_OBJECT
public:
    explicit PresentationController(ShowModel::Presentation *deck, QObject *parent = nullptr);

    void goTo(int slide, int build = 0);
    void advance(int delta);

    [[nodiscard]] ShowModel::Presentation *deck() const { return m_deck; }
    [[nodiscard]] int slide() const { return m_slide; }
    [[nodiscard]] int build() const { return m_build; }
    [[nodiscard]] int buildCap(int slideIndex) const;
    [[nodiscard]] bool atStart() const { return m_slide == 0 && m_build == 0; }
    [[nodiscard]] bool atEnd() const;

signals:
    // Emitted after any state change; observers re-render.
    void stateChanged();

private:
    ShowModel::Presentation *m_deck;
    int m_slide = 0;
    int m_build = 0;
};

} // namespace QindaOffice::Apps::Show
