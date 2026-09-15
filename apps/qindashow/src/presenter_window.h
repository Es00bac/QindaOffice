// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PresenterWindow (plan section 7.5): the speaker view on the second
// screen while the SlidePlayer owns the projector. Shows the current
// slide, the next-slide preview, speaker notes, an elapsed timer, the
// wall clock, and the slide counter. Observes the same
// PresentationController as the player.

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QScreen;
class QTextBrowser;

namespace QindaOffice::Apps::Show {

class PresentationController;

class PresenterWindow final : public QWidget {
    Q_OBJECT
public:
    PresenterWindow(PresentationController *controller, QScreen *screen,
                    QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refresh();
    void tickClock();

    PresentationController *m_controller;
    QLabel *m_next;
    QLabel *m_timer;
    QLabel *m_clock;
    QLabel *m_counter;
    QTextBrowser *m_notes;
    QElapsedTimer m_elapsed;
};

} // namespace QindaOffice::Apps::Show
