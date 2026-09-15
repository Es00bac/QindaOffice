// SPDX-License-Identifier: GPL-3.0-or-later
#include "presenter_window.h"

#include "presentation_controller.h"
#include "slide_renderer.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QTextBrowser>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace QindaOffice::Apps::Show {

PresenterWindow::PresenterWindow(PresentationController *controller, QScreen *screen,
                                 QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (screen != nullptr && windowHandle() != nullptr) {
        windowHandle()->setScreen(screen);
    }
    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_next = new QLabel(this);
    m_next->setMinimumSize(240, 135);
    m_next->setFrameShape(QFrame::StyledPanel);
    top->addStretch(1);
    top->addWidget(m_next, 0, Qt::AlignCenter);
    layout->addLayout(top, 1);

    m_notes = new QTextBrowser(this);
    layout->addWidget(m_notes, 2);

    auto *status = new QHBoxLayout;
    m_timer = new QLabel(this);
    m_clock = new QLabel(this);
    m_counter = new QLabel(this);
    status->addWidget(m_timer);
    status->addStretch(1);
    status->addWidget(m_counter);
    status->addStretch(1);
    status->addWidget(m_clock);
    layout->addLayout(status);

    auto *ticker = new QTimer(this);
    connect(ticker, &QTimer::timeout, this, &PresenterWindow::tickClock);
    ticker->start(1000);
    m_elapsed.start();

    connect(controller, &PresentationController::stateChanged, this,
            &PresenterWindow::refresh);
    refresh();
    tickClock();
}

void PresenterWindow::refresh()
{
    const ShowModel::Presentation *deck = m_controller->deck();
    const int slide = m_controller->slide();
    const QString notes = slide < deck->slides.size() ? deck->slides.at(slide).notes : QString();
    m_notes->setPlainText(notes);
    m_counter->setText(tr("Slide %1 / %2").arg(slide + 1).arg(deck->slides.size()));
    const int next = slide + 1;
    m_next->setPixmap(next < deck->slides.size()
                          ? ShowModel::renderSlideToPixmap(deck->slides.at(next), deck->slideSize,
                                                           m_next->minimumSize())
                          : QPixmap());
    update();
}

void PresenterWindow::tickClock()
{
    const qint64 seconds = m_elapsed.elapsed() / 1000;
    m_timer->setText(tr("%1:%2")
                         .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                         .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    m_clock->setText(QTime::currentTime().toString(QStringLiteral("hh:mm")));
    update();
}

void PresenterWindow::paintEvent(QPaintEvent *)
{
    const ShowModel::Presentation *deck = m_controller->deck();
    const int slide = m_controller->slide();
    if (deck == nullptr || slide < 0 || slide >= deck->slides.size()) {
        return;
    }
    QPainter painter(this);
    // Current slide occupies the top strip, left of the next preview.
    const QRectF target(8, 8, width() * 0.52, height() * 0.40);
    const QSizeF size = deck->slideSize;
    const qreal scale = qMin(target.width() / size.width(), target.height() / size.height());
    painter.translate(target.center() - QPointF(size.width() * scale / 2.0,
                                                size.height() * scale / 2.0));
    painter.scale(scale, scale);
    ShowModel::renderSlide(&painter, deck->slides.at(slide), size, m_controller->build());
}

} // namespace QindaOffice::Apps::Show
