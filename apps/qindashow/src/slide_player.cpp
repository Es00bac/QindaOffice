// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_player.h"

#include "slide_renderer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace QindaOffice::Apps::Show {

SlidePlayer::SlidePlayer(ShowModel::Presentation *deck, int fromSlide, QWidget *parent)
    : QWidget(parent)
    , m_deck(deck)
    , m_slide(qBound(0, fromSlide, deck->slides.size() - 1))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::BlankCursor);
    ensureStepBounds();
}

int SlidePlayer::maxBuild(int slideIndex) const
{
    if (slideIndex < 0 || slideIndex >= m_deck->slides.size()) {
        return 0;
    }
    int highest = 0;
    for (const ShowModel::Shape &shape : m_deck->slides.at(slideIndex).shapes) {
        highest = qMax(highest, shape.buildStep);
    }
    return highest;
}

void SlidePlayer::ensureStepBounds()
{
    m_buildCap = maxBuild(m_slide);
    m_build = qBound(0, m_build, m_buildCap);
}

void SlidePlayer::advance(int delta)
{
    m_blank = false;
    if (delta > 0) {
        if (m_build < m_buildCap) {
            ++m_build; // reveal the next build
        } else if (m_slide < m_deck->slides.size() - 1) {
            ++m_slide; // then move to the next slide, fresh at build 0
            m_build = 0;
        } else {
            return; // past the last slide is a no-op
        }
    } else {
        if (m_build > 0) {
            --m_build;
        } else if (m_slide > 0) {
            --m_slide; // previous slide, fully revealed
            m_build = maxBuild(m_slide);
        } else {
            return;
        }
    }
    ensureStepBounds();
    update();
}

void SlidePlayer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_blank) {
        painter.fillRect(rect(), m_blankColor);
        return;
    }
    if (m_slide < 0 || m_slide >= m_deck->slides.size()) {
        painter.fillRect(rect(), Qt::black);
        return;
    }
    const QSizeF size = m_deck->slideSize;
    painter.save();
    painter.translate(rect().center());
    const qreal scale = qMin(width() / size.width(), height() / size.height());
    painter.scale(scale, scale);
    painter.translate(-size.width() / 2.0, -size.height() / 2.0);
    renderSlide(&painter, m_deck->slides.at(m_slide), size, m_build);
    painter.restore();
}

void SlidePlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        advance(1);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        advance(-1);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        close();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SlidePlayer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Q:
        close();
        return;
    case Qt::Key_Space:
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Down:
        advance(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Up:
    case Qt::Key_Backspace:
        advance(-1);
        return;
    case Qt::Key_B:
        m_blank = true;
        m_blankColor = Qt::black;
        update();
        return;
    case Qt::Key_W:
        m_blank = true;
        m_blankColor = Qt::white;
        update();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace QindaOffice::Apps::Show
