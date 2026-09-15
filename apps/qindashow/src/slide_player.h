// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow player (plan section 7.5): a fullscreen window on the
// chosen screen, driven by a shared PresentationController. Transitions
// (cut/fade/slide/zoom) animate the incoming slide over 350 ms OutCubic
// and are disabled entirely when the desktop reports
// accessibility.reducedMotion. Keys: Space/arrows advance, Backspace back,
// B/W blank, P pen and L laser annotations (ephemeral vectors, kept per
// slide for the session), E erases the current slide's ink, Esc quits.

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include <QHash>
#include <QWidget>

class QPropertyAnimation;
class QScreen;

namespace QindaOffice::Apps::Show {

class PresentationController;

class SlidePlayer final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal transitionProgress READ transitionProgress WRITE setTransitionProgress)
public:
    SlidePlayer(PresentationController *controller, QScreen *screen, QWidget *parent = nullptr);

    [[nodiscard]] qreal transitionProgress() const { return m_progress; }
    void setTransitionProgress(qreal progress);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum class Tool { None, Pen, Laser };

    void keyAdvance(int delta);
    void beginTransition();
    [[nodiscard]] QPixmap renderSlideFrame(int slideIndex) const;
    void paintTransition(QPainter *painter);
    void paintInk(QPainter *painter);

    PresentationController *m_controller;
    QPixmap m_current;
    QPixmap m_previous;
    QPropertyAnimation *m_animation = nullptr;
    qreal m_progress = 1.0;
    bool m_reducedMotion = false;
    bool m_blank = false;
    Qt::GlobalColor m_blankColor = Qt::black;
    Tool m_tool = Tool::None;
    QList<QPolygonF> m_strokes;
    QHash<int, QList<QPolygonF>> m_inkBySlide;
    QPolygonF m_activeStroke;
    QPointF m_laserPos;
    bool m_laserVisible = false;
};

} // namespace QindaOffice::Apps::Show
