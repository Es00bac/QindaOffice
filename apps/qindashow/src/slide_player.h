// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow player (plan section 6.3): a fullscreen window that
// renders the deck through show_model::renderSlide. Click/Right/space
// advance through builds then slides; Backspace goes back; Esc quits; B/W
// blank the screen black/white. M9 adds the presenter view, transitions
// and ink annotations.

#include "presentation.h"

#include <QWidget>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

class SlidePlayer final : public QWidget {
    Q_OBJECT
public:
    SlidePlayer(ShowModel::Presentation *deck, int fromSlide, QWidget *parent = nullptr);

    [[nodiscard]] int currentSlide() const { return m_slide; }
    [[nodiscard]] int currentBuild() const { return m_build; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void advance(int delta);
    void ensureStepBounds();
    [[nodiscard]] int maxBuild(int slideIndex) const;

    ShowModel::Presentation *m_deck;
    int m_slide = 0;
    // (test-facing state readers live in the public section above)
    int m_build = 0;      // how many build steps are revealed
    int m_buildCap = 0;   // total build steps on the current slide
    bool m_blank = false;
    Qt::GlobalColor m_blankColor = Qt::black;
};

} // namespace QindaOffice::Apps::Show
