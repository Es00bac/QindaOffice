// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PresentationController (plan section 7.5): the (slideIndex, buildStep)
// pair that drives the player and the presenter. Advancing reveals builds
// before slides; going back shows the previous slide fully revealed;
// hidden slides are skipped (M62). Pure logic over a live Presentation so
// tests exercise it without widgets.
//
// M91 (D-099): the show follows Set Up Slide Show (show_settings.h) — its
// order (every slide, a range or a custom show: showPlaylist), looping
// until Esc, a kiosk no click advances, no animation, the slides' timings
// (autoAdvanceMs, split evenly over a slide's builds), the pen and laser
// colours — and runs action settings (show_action.h): a click on a shape
// or on linked text, or the mouse passing over a shape (slide_hit.h);
// next / previous / first / last / last viewed slide, a slide, a custom
// show (with "show and return"), the end of the show. What leaves the show
// is a signal the host answers: an address or a file to open
// (openRequested), a sound, a highlight, a notice. A program or a macro is
// NEVER run: noticeRequested says so instead (AGENT-GUARD, show_action.h).
// Rehearse Timings adds a ShowRehearsal clock (show_rehearsal.h).

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include <QColor>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace QindaOffice::Apps::Show {

class ShowRehearsal;

class PresentationController final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the host when a show starts.")
    Q_MOC_INCLUDE("show_rehearsal.h")
    Q_PROPERTY(int slide READ slide NOTIFY stateChanged)
    Q_PROPERTY(int build READ build NOTIFY stateChanged)
    Q_PROPERTY(int slideCount READ slideCount NOTIFY stateChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY stateChanged)
    Q_PROPERTY(QString transition READ transition NOTIFY stateChanged)
    // The longest animationMs among the shapes arriving at the current
    // build step (0 when they simply appear).
    Q_PROPERTY(int buildDurationMs READ buildDurationMs NOTIFY stateChanged)
    // ---- M91 (D-099) ----
    // Where the show is in its own order (1-based; 0 off it — a hidden slide
    // a link jumped to) and how many slides that order plays.
    Q_PROPERTY(int position READ position NOTIFY stateChanged)
    Q_PROPERTY(int showLength READ showLength NOTIFY stateChanged)
    // The slide Next goes to once this one's builds are done (-1: the end) —
    // the presenter view's preview.
    Q_PROPERTY(int nextSlide READ nextSlide NOTIFY stateChanged)
    // How long until the show advances by itself (-1: by hand).
    Q_PROPERTY(int autoAdvanceMs READ autoAdvanceMs NOTIFY stateChanged)
    // Whether a click (not on an action) advances.
    Q_PROPERTY(bool clickAdvances READ clickAdvances NOTIFY stateChanged)
    Q_PROPERTY(bool kiosk READ kiosk CONSTANT)
    Q_PROPERTY(bool windowed READ windowed CONSTANT)
    Q_PROPERTY(int kioskRestartMs READ kioskRestartMs CONSTANT)
    Q_PROPERTY(QColor penColor READ penColor CONSTANT)
    Q_PROPERTY(QColor laserColor READ laserColor CONSTANT)
    // The custom show playing ("" = the deck's own order).
    Q_PROPERTY(QString showName READ showName NOTIFY stateChanged)
    // Non-null while rehearsing timings.
    Q_PROPERTY(QindaOffice::Apps::Show::ShowRehearsal *rehearsal READ rehearsal CONSTANT)
public:
    struct Options {
        int startSlide = -1;   // From Current Slide: this deck position (else the show's first)
        int customShow = -1;   // play this custom show (index) instead of the settings' order
        bool rehearse = false; // Rehearse Timings: by hand, the rehearsal clock running
    };
    explicit PresentationController(ShowModel::Presentation *deck, QObject *parent = nullptr);
    PresentationController(ShowModel::Presentation *deck, const Options &options, QObject *parent = nullptr);
    ~PresentationController() override;

    Q_INVOKABLE void goTo(int slide, int build = 0);
    Q_INVOKABLE void advance(int delta);
    [[nodiscard]] int slideCount() const { return int(m_deck->slides.size()); }
    [[nodiscard]] QString notes() const;
    [[nodiscard]] QString transition() const;
    [[nodiscard]] int buildDurationMs() const;

    [[nodiscard]] ShowModel::Presentation *deck() const { return m_deck; }
    [[nodiscard]] int slide() const { return m_slide; }
    [[nodiscard]] int build() const { return m_build; }
    [[nodiscard]] int buildCap(int slideIndex) const;
    [[nodiscard]] bool atStart() const { return m_slide == 0 && m_build == 0; }
    [[nodiscard]] bool atEnd() const;

    // ---- M91 (D-099) ----
    [[nodiscard]] int position() const { return m_position + 1; }
    [[nodiscard]] int showLength() const { return int(m_playlist.size()); }
    [[nodiscard]] int nextSlide() const;
    [[nodiscard]] int autoAdvanceMs() const;
    [[nodiscard]] bool clickAdvances() const;
    [[nodiscard]] bool kiosk() const { return m_settings.type == ShowModel::ShowType::Kiosk; }
    [[nodiscard]] bool windowed() const { return m_settings.type == ShowModel::ShowType::Window; }
    [[nodiscard]] int kioskRestartMs() const { return m_settings.kioskRestartMs; }
    [[nodiscard]] QColor penColor() const { return m_settings.penColor; }
    [[nodiscard]] QColor laserColor() const { return m_settings.laserColor; }
    [[nodiscard]] QString showName() const { return m_showName; }
    [[nodiscard]] ShowRehearsal *rehearsal() const { return m_rehearsal; }
    [[nodiscard]] const QList<int> &playlist() const { return m_playlist; }
    // A click at (x, y) in slide points: runs the action there; false when
    // there is none (the caller advances, if clicks do).
    Q_INVOKABLE bool click(qreal x, qreal y);
    // The pointer at (x, y): runs a mouse-over action when it enters its
    // shape; true when an action (click or mouse-over) is under it.
    Q_INVOKABLE bool hover(qreal x, qreal y);
    // The slide's timing ran out: the next build or slide.
    Q_INVOKABLE void timedAdvance();
    // A kiosk left alone: back to the first slide of its order.
    Q_INVOKABLE void restart();
    // Runs `action` (bounds: where its shape is, for the highlight); false
    // when there is nothing to do.
    bool runAction(const ShowModel::ShowAction &action, const QRectF &bounds = QRectF());
    // A file URL holding a sound's bytes (the host's media files).
    void setSoundResolver(std::function<QUrl(const QByteArray &, const QString &)> resolver)
    {
        m_soundResolver = std::move(resolver);
    }

signals:
    // Emitted after any state change; observers re-render.
    void stateChanged();
    // ---- M91 ----
    void endReached();    // advanced past the last slide (not looping)
    void endRequested();  // an End Show action
    void openRequested(const QString &address, bool file);
    void noticeRequested(const QString &text);
    void soundRequested(const QUrl &url);
    void stopSoundsRequested();
    void highlightRequested(const QRectF &slideRect);

private:
    struct Frame {
        QList<int> playlist;
        int position = -1;
        int slide = 0;
        int build = 0;
        QString name;
    };

    void enter(int slide, int build);
    void moveTo(int position, int build);
    void goNext();
    void goPrevious();
    [[nodiscard]] int nextPosition() const;
    [[nodiscard]] int previousPosition() const;
    [[nodiscard]] bool animations() const;
    void playCustomShow(quint32 number, bool returnAfter);

    ShowModel::Presentation *m_deck;
    ShowModel::ShowSettings m_settings;
    QList<int> m_playlist;
    QList<Frame> m_returns; // custom shows playing with "show and return"
    QList<int> m_history;   // slides seen, for Last Slide Viewed
    QString m_showName;
    QString m_hoverShape;
    ShowRehearsal *m_rehearsal = nullptr;
    std::function<QUrl(const QByteArray &, const QString &)> m_soundResolver;
    int m_slide = 0;
    int m_build = 0;
    int m_position = -1;
};

} // namespace QindaOffice::Apps::Show
