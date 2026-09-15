// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_player.h"

#include "desktop_settings.h"
#include "presentation_controller.h"
#include "slide_renderer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWindow>

namespace QindaOffice::Apps::Show {

namespace {
constexpr int kTransitionMs = 350;
}

SlidePlayer::SlidePlayer(PresentationController *controller, QScreen *screen, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_reducedMotion(Shell::DesktopSettings::reducedMotion())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::BlankCursor);
    m_current = renderSlideFrame(controller->slide());
    if (screen != nullptr && windowHandle() != nullptr) {
        windowHandle()->setScreen(screen);
    }
    connect(controller, &PresentationController::stateChanged, this,
            [this] { beginTransition(); });
}

QPixmap SlidePlayer::renderSlideFrame(int slideIndex) const
{
    const ShowModel::Presentation *deck = m_controller->deck();
    QPixmap pixmap(size().isValid() ? size() : QSize(960, 540));
    pixmap.fill(Qt::black);
    if (deck == nullptr || slideIndex < 0 || slideIndex >= deck->slides.size()) {
        return pixmap;
    }
    QPainter painter(&pixmap);
    // Letterbox the slide into the widget.
    const QSizeF slide = deck->slideSize;
    const qreal scale = qMin(width() / slide.width(), height() / slide.height());
    painter.translate((width() - slide.width() * scale) / 2.0,
                      (height() - slide.height() * scale) / 2.0);
    painter.scale(scale, scale);
    ShowModel::renderSlide(&painter, deck->slides.at(slideIndex), slide,
                           m_controller->build());
    return pixmap;
}

void SlidePlayer::beginTransition()
{
    m_strokes = m_inkBySlide.value(m_controller->slide());
    m_activeStroke.clear();
    m_laserVisible = false;
    if (m_reducedMotion || m_previous.isNull()) {
        m_current = renderSlideFrame(m_controller->slide());
        m_previous = QPixmap();
        m_progress = 1.0;
        update();
        return;
    }
    // m_current still holds the outgoing frame; the transition blends it
    // with the freshly rendered incoming one.
    m_previous = m_current;
    m_current = renderSlideFrame(m_controller->slide());
    m_progress = 0.0;
    delete m_animation;
    m_animation = new QPropertyAnimation(this, "transitionProgress", this);
    m_animation->setDuration(kTransitionMs);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, [this] { m_previous = QPixmap(); });
    m_animation->start();
    update();
}

void SlidePlayer::setTransitionProgress(qreal progress)
{
    m_progress = qBound<qreal>(0.0, progress, 1.0);
    update();
}

void SlidePlayer::paintTransition(QPainter *painter)
{
    painter->drawPixmap(rect(), m_previous);
    const ShowModel::Presentation *deck = m_controller->deck();
    const QString transition = deck != nullptr
        ? deck->slides.at(qBound(0, m_controller->slide(), deck->slides.size() - 1)).transition
        : QStringLiteral("cut");
    if (transition == QLatin1String("fade")) {
        painter->save();
        painter->setOpacity(m_progress);
        painter->drawPixmap(rect(), m_current);
        painter->restore();
    } else if (transition == QLatin1String("slide")) {
        const int offset = qRound((1.0 - m_progress) * width());
        painter->drawPixmap(rect().translated(offset, 0), m_current);
    } else if (transition == QLatin1String("zoom")) {
        painter->save();
        painter->setOpacity(m_progress);
        const qreal scale = 1.0 + 0.15 * (1.0 - m_progress);
        const QSizeF zoomed = QSizeF(m_current.size()) * scale;
        const QRectF target(rect().center() - QPointF(zoomed.width() / 2.0,
                                                      zoomed.height() / 2.0),
                            zoomed);
        painter->drawPixmap(target, m_current, QRectF(QPointF(0, 0), QSizeF(m_current.size())));
        painter->restore();
    } else { // cut and unknown names: hard swap at the end of the blend
        if (m_progress >= 1.0) {
            painter->drawPixmap(rect(), m_current);
        }
    }
}

void SlidePlayer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_blank) {
        painter.fillRect(rect(), m_blankColor);
        return;
    }
    if (!m_previous.isNull() && m_progress < 1.0) {
        paintTransition(&painter);
    } else {
        painter.drawPixmap(rect(), m_current);
    }
    paintInk(&painter);
    if (m_laserVisible && m_tool == Tool::Laser) {
        painter.setPen(QPen(QColor(255, 40, 40), 2));
        painter.setBrush(QColor(255, 40, 40));
        painter.drawEllipse(m_laserPos, 5, 5);
    }
}

void SlidePlayer::paintInk(QPainter *painter)
{
    painter->save();
    painter->setPen(QPen(QColor(220, 30, 30), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QPolygonF &stroke : std::as_const(m_strokes)) {
        painter->drawPolyline(stroke);
    }
    if (!m_activeStroke.isEmpty()) {
        painter->drawPolyline(m_activeStroke);
    }
    painter->restore();
}

void SlidePlayer::keyAdvance(int delta)
{
    m_blank = false;
    m_laserVisible = false;
    if (m_activeStroke.size() > 1) {
        m_strokes.append(m_activeStroke);
    }
    m_activeStroke.clear();
    m_inkBySlide.insert(m_controller->slide(), m_strokes);
    m_controller->advance(delta);
}

void SlidePlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_tool == Tool::Pen) {
            m_activeStroke = QPolygonF({event->position()});
            event->accept();
            return;
        }
        keyAdvance(1);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        keyAdvance(-1);
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

void SlidePlayer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_tool == Tool::Pen && !m_activeStroke.isEmpty()) {
        m_activeStroke.append(event->position());
        update();
        event->accept();
        return;
    }
    if (m_tool == Tool::Laser) {
        m_laserPos = event->position();
        m_laserVisible = true;
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SlidePlayer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_tool == Tool::Pen && m_activeStroke.size() > 1) {
        m_strokes.append(m_activeStroke);
        m_activeStroke.clear();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
    case Qt::Key_Enter:
    case Qt::Key_Return:
        keyAdvance(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Up:
    case Qt::Key_Backspace:
        keyAdvance(-1);
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
    case Qt::Key_P:
        m_tool = m_tool == Tool::Pen ? Tool::None : Tool::Pen;
        setCursor(m_tool == Tool::None ? Qt::BlankCursor : Qt::CrossCursor);
        return;
    case Qt::Key_L:
        m_tool = m_tool == Tool::Laser ? Tool::None : Tool::Laser;
        setCursor(m_tool == Tool::None ? Qt::BlankCursor : Qt::CrossCursor);
        return;
    case Qt::Key_E:
        m_strokes.clear();
        m_activeStroke.clear();
        update();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace QindaOffice::Apps::Show
