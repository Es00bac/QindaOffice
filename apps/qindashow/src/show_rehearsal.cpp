// SPDX-License-Identifier: GPL-3.0-or-later
// The Rehearse Timings clock (show_rehearsal.h).

#include "show_rehearsal.h"

namespace QindaOffice::Apps::Show {

QString rehearsalClock(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds % 3600 / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

ShowRehearsal::ShowRehearsal(QObject *parent)
    : QObject(parent)
{
    m_elapsed.start();
    m_runningSince = now();
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, &ShowRehearsal::ticked);
    m_tick.start();
}

void ShowRehearsal::setClock(Clock clock)
{
    m_clock = std::move(clock);
    m_runningSince = now();
    m_slideBase = 0;
    emit ticked();
}

qint64 ShowRehearsal::now() const
{
    return m_clock ? m_clock() : m_elapsed.elapsed();
}

qint64 ShowRehearsal::slideMs() const
{
    return m_slideBase + (m_paused || m_finished ? 0 : now() - m_runningSince);
}

qint64 ShowRehearsal::totalMs() const
{
    return m_doneMs + slideMs();
}

QString ShowRehearsal::slideText() const
{
    return rehearsalClock(slideMs());
}

QString ShowRehearsal::totalText() const
{
    return rehearsalClock(totalMs());
}

void ShowRehearsal::record()
{
    if (m_slide < 0) {
        return;
    }
    const qint64 spent = slideMs();
    m_timings.insert(m_slide, spent); // the last visit counts
    m_doneMs += spent;
}

void ShowRehearsal::slideEntered(int slide)
{
    if (m_finished || slide == m_slide) {
        return;
    }
    record();
    m_slide = slide;
    m_slideBase = 0;
    m_runningSince = now();
    emit ticked();
}

void ShowRehearsal::finish()
{
    if (m_finished) {
        return;
    }
    record();
    m_slideBase = 0;
    m_finished = true;
    m_tick.stop();
    emit ticked();
}

void ShowRehearsal::togglePause()
{
    if (m_finished) {
        return;
    }
    if (m_paused) {
        m_runningSince = now();
    } else {
        m_slideBase += now() - m_runningSince;
    }
    m_paused = !m_paused;
    emit pausedChanged();
    emit ticked();
}

void ShowRehearsal::repeat()
{
    if (m_finished) {
        return;
    }
    m_slideBase = 0;
    m_runningSince = now();
    emit ticked();
}

} // namespace QindaOffice::Apps::Show
