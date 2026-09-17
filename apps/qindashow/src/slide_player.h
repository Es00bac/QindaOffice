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
    Q_PROPERTY(qreal buildProgress READ buildProgress WRITE setBuildProgress)
public:
    SlidePlayer(PresentationController *controller, QScreen *screen, QWidget *parent = nullptr);

    [[nodiscard]] qreal transitionProgress() const { return m_progress; }
    void setTransitionProgress(qreal progress);
    // 0..1 while the shapes at the current build step animate in. Separate
    // from the slide transition: revealing a bullet is not a slide change,
    // and blending two whole frames for it would flash the rest of the
    // slide.
    [[nodiscard]] qreal buildProgress() const { return m_buildProgress; }
    void setBuildProgress(qreal progress);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Tool { None, Pen, Laser };

    void keyAdvance(int delta);
    void beginTransition();
    void beginBuildAnimation();
    [[nodiscard]] QPixmap renderSlideFrame(int slideIndex) const;
    [[nodiscard]] QPixmap renderSlideFrame(int slideIndex, int buildStep,
                                           qreal buildProgress) const;
    // The longest animationMs among the shapes arriving at `buildStep`.
    [[nodiscard]] int buildDurationMs(int slideIndex, int buildStep) const;
    void paintTransition(QPainter *painter);
    void paintInk(QPainter *painter);

    PresentationController *m_controller;
    QScreen *m_targetScreen = nullptr; // applied in showEvent (no handle in ctors)
    QPixmap m_current;
    QPixmap m_previous;
    QPropertyAnimation *m_animation = nullptr;
    QPropertyAnimation *m_buildAnimation = nullptr;
    qreal m_progress = 1.0;
    qreal m_buildProgress = 1.0;
    // The state the last frame was rendered for, so a build advance can be
    // told apart from a slide change.
    int m_lastSlide = -1;
    int m_lastBuild = 0;
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
