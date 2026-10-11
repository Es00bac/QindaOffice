// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Rehearse Timings (M91, D-099): the clock behind the show's Recording
// toolbar (RehearsalBar.qml) — the time on the current slide and the
// whole rehearsal, Pause (neither clock runs), Repeat (the current
// slide's clock starts again; what it had is taken off the total), Next
// (the controller advances). Each slide's time is its LAST visit's, as
// PowerPoint records it; at the end the host asks whether to keep them
// (ShowSlideShow::finishRehearsal) and writes them as each slide's
// Advance Slide timing in one undo step.
//
// AGENT-NOTE: the clock is injectable (setClock) so the tests drive time
// deterministically; the default is a monotonic QElapsedTimer. `ticked`
// fires about five times a second while running, for the toolbar.

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace QindaOffice::Apps::Show {

class ShowRehearsal final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the show when rehearsing.")
    Q_PROPERTY(qint64 slideMs READ slideMs NOTIFY ticked)
    Q_PROPERTY(qint64 totalMs READ totalMs NOTIFY ticked)
    Q_PROPERTY(QString slideText READ slideText NOTIFY ticked)
    Q_PROPERTY(QString totalText READ totalText NOTIFY ticked)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
public:
    using Clock = std::function<qint64()>;
    explicit ShowRehearsal(QObject *parent);

    // Tests: a clock in milliseconds.
    void setClock(Clock clock);
    // The show moved to `slide` (deck position): the slide left keeps its time.
    void slideEntered(int slide);
    // The show ends: the current slide keeps its time too.
    void finish();

    [[nodiscard]] qint64 slideMs() const;
    [[nodiscard]] qint64 totalMs() const;
    [[nodiscard]] QString slideText() const;
    [[nodiscard]] QString totalText() const;
    [[nodiscard]] bool paused() const { return m_paused; }
    // Deck position → milliseconds, for the slides rehearsed.
    [[nodiscard]] QHash<int, qint64> timings() const { return m_timings; }

    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void repeat();

signals:
    void ticked();
    void pausedChanged();

private:
    [[nodiscard]] qint64 now() const;
    void record();

    Clock m_clock;
    QElapsedTimer m_elapsed;
    QTimer m_tick;
    QHash<int, qint64> m_timings;
    qint64 m_doneMs = 0;      // the slides left behind
    qint64 m_slideBase = 0;   // the current slide's time before its last resume
    qint64 m_runningSince = 0;
    int m_slide = -1;
    bool m_paused = false;
    bool m_finished = false;
};

// "0:00:05" — the Recording toolbar's clock.
[[nodiscard]] QString rehearsalClock(qint64 ms);

} // namespace QindaOffice::Apps::Show
