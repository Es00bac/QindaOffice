// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation_controller.h"

#include "show_rehearsal.h"
#include "slide_hit.h"

namespace QindaOffice::Apps::Show {

PresentationController::PresentationController(ShowModel::Presentation *deck, QObject *parent)
    : PresentationController(deck, Options(), parent)
{
}

PresentationController::PresentationController(ShowModel::Presentation *deck, const Options &options, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_settings(deck->showSettings.normalized())
{
    if (options.rehearse) {
        // Rehearsing is presenting by hand, once through, with the clock.
        m_settings.type = ShowModel::ShowType::Speaker;
        m_settings.loop = false;
        m_settings.useTimings = false;
        m_rehearsal = new ShowRehearsal(this);
    }
    m_playlist = ShowModel::showPlaylist(*deck, options.customShow);
    if (options.customShow >= 0 && options.customShow < deck->customShows.size()) {
        m_showName = deck->customShows.at(options.customShow).name;
    } else if (m_settings.range == ShowModel::ShowRange::Custom) {
        const int index = ShowModel::customShowIndex(*deck, m_settings.customShow);
        m_showName = index >= 0 ? deck->customShows.at(index).name : QString();
    }
    int start = m_playlist.isEmpty() ? 0 : m_playlist.first();
    if (options.startSlide >= 0) {
        for (const int row : std::as_const(m_playlist)) {
            if (row >= options.startSlide) {
                start = row;
                break;
            }
        }
    }
    m_position = int(m_playlist.indexOf(start));
    enter(qBound(0, start, qMax(0, int(deck->slides.size()) - 1)), 0);
}

PresentationController::~PresentationController() = default;

int PresentationController::buildCap(int slideIndex) const
{
    if (slideIndex < 0 || slideIndex >= m_deck->slides.size()) {
        return 0;
    }
    int highest = 0;
    for (const ShowModel::Shape &shape : m_deck->slides.at(slideIndex).shapes) {
        highest = qMax(highest, qMax(shape.buildStep, shape.exit.step)); // M91: exits are clicks too
    }
    return highest;
}

QString PresentationController::notes() const
{
    return m_slide >= 0 && m_slide < m_deck->slides.size() ? m_deck->slides.at(m_slide).notes : QString();
}

QString PresentationController::transition() const
{
    return m_slide >= 0 && m_slide < m_deck->slides.size() ? m_deck->slides.at(m_slide).transition
                                                            : QStringLiteral("cut");
}

int PresentationController::buildDurationMs() const
{
    if (m_slide < 0 || m_slide >= m_deck->slides.size()) {
        return 0;
    }
    int longest = 0;
    for (const ShowModel::Shape &shape : m_deck->slides.at(m_slide).shapes) {
        if (shape.buildStep == m_build && shape.animation != ShowModel::ShapeAnimation::Appear) {
            longest = qMax(longest, shape.animationMs);
        }
        if (shape.exit.step == m_build && shape.exit.isSet() && shape.exit.animation != ShowModel::ShapeAnimation::Appear) {
            longest = qMax(longest, shape.exit.durationMs);
        }
    }
    return longest;
}

bool PresentationController::animations() const
{
    return m_settings.animation;
}

bool PresentationController::atEnd() const
{
    const bool looping = (m_settings.loop || kiosk()) && m_rehearsal == nullptr && !m_playlist.isEmpty();
    return m_build >= buildCap(m_slide) && nextPosition() < 0 && m_returns.isEmpty() && !looping;
}

// ---- the show's order -------------------------------------------------------------

int PresentationController::nextPosition() const
{
    if (m_position >= 0) {
        return m_position + 1 < m_playlist.size() ? m_position + 1 : -1;
    }
    // Off the order (a hidden slide a link jumped to): what follows it in the deck.
    for (qsizetype i = 0; i < m_playlist.size(); ++i) {
        if (m_playlist.at(i) > m_slide) {
            return int(i);
        }
    }
    return -1;
}

int PresentationController::previousPosition() const
{
    if (m_position >= 0) {
        return m_position - 1;
    }
    for (qsizetype i = m_playlist.size() - 1; i >= 0; --i) {
        if (m_playlist.at(i) < m_slide) {
            return int(i);
        }
    }
    return -1;
}

int PresentationController::nextSlide() const
{
    if (const int next = nextPosition(); next >= 0) {
        return m_playlist.at(next);
    }
    if (!m_returns.isEmpty()) {
        return m_returns.last().slide;
    }
    const bool looping = (m_settings.loop || kiosk()) && m_rehearsal == nullptr;
    return looping && !m_playlist.isEmpty() ? m_playlist.first() : -1;
}

void PresentationController::enter(int slide, int build)
{
    m_slide = qBound(0, slide, qMax(0, int(m_deck->slides.size()) - 1));
    const int cap = buildCap(m_slide);
    m_build = animations() ? qBound(0, build, cap) : cap;
    if (m_history.isEmpty() || m_history.last() != m_slide) {
        m_history.append(m_slide);
    }
    m_hoverShape.clear();
    if (m_rehearsal != nullptr) {
        m_rehearsal->slideEntered(m_slide);
    }
}

void PresentationController::moveTo(int position, int build)
{
    m_position = position;
    enter(m_playlist.at(position), build);
}

void PresentationController::goTo(int slide, int build)
{
    const int bounded = qBound(0, slide, qMax(0, int(m_deck->slides.size()) - 1));
    m_position = int(m_playlist.indexOf(bounded));
    enter(bounded, build);
    emit stateChanged();
}

void PresentationController::goNext()
{
    int next = nextPosition();
    if (next < 0) {
        if (!m_returns.isEmpty()) {
            // "Show and return": back on the slide that started the custom show.
            const Frame frame = m_returns.takeLast();
            m_playlist = frame.playlist;
            m_showName = frame.name;
            m_position = frame.position;
            enter(frame.slide, frame.build);
            emit stateChanged();
            return;
        }
        const bool looping = (m_settings.loop || kiosk()) && m_rehearsal == nullptr;
        if (!looping || m_playlist.isEmpty()) {
            emit endReached(); // past the last slide: the player shows the end
            return;
        }
        next = 0;
    }
    moveTo(next, 0);
    emit stateChanged();
}

void PresentationController::goPrevious()
{
    const int previous = previousPosition();
    if (previous < 0) {
        if (!m_returns.isEmpty()) {
            const Frame frame = m_returns.takeLast();
            m_playlist = frame.playlist;
            m_showName = frame.name;
            m_position = frame.position;
            enter(frame.slide, frame.build);
            emit stateChanged();
        }
        return;
    }
    moveTo(previous, buildCap(m_playlist.at(previous))); // fully revealed
    emit stateChanged();
}

void PresentationController::advance(int delta)
{
    if (delta > 0) {
        if (animations() && m_build < buildCap(m_slide)) {
            ++m_build;
            emit stateChanged();
            return;
        }
        goNext();
    } else if (delta < 0) {
        if (animations() && m_build > 0) {
            --m_build;
            emit stateChanged();
            return;
        }
        goPrevious();
    }
}

int PresentationController::autoAdvanceMs() const
{
    if (m_rehearsal != nullptr || !m_settings.useTimings || m_slide < 0 || m_slide >= m_deck->slides.size()) {
        return -1;
    }
    const int timing = m_deck->slides.at(m_slide).advanceAfterMs;
    if (timing < 0) {
        return -1;
    }
    // A slide's builds play evenly spread over its timing.
    const int steps = animations() ? buildCap(m_slide) + 1 : 1;
    return timing / qMax(1, steps);
}

bool PresentationController::clickAdvances() const
{
    if (kiosk()) {
        return false;
    }
    const bool slideSays = m_slide < 0 || m_slide >= m_deck->slides.size() || m_deck->slides.at(m_slide).advanceOnClick;
    return slideSays || !m_settings.useTimings || m_rehearsal != nullptr;
}

void PresentationController::timedAdvance()
{
    advance(1);
}

void PresentationController::restart()
{
    while (!m_returns.isEmpty()) {
        const Frame frame = m_returns.takeLast();
        m_playlist = frame.playlist;
        m_showName = frame.name;
    }
    if (!m_playlist.isEmpty()) {
        moveTo(0, 0);
        emit stateChanged();
    }
}

// ---- actions ----------------------------------------------------------------------

bool PresentationController::click(qreal x, qreal y)
{
    if (m_slide < 0 || m_slide >= m_deck->slides.size()) {
        return false;
    }
    const std::optional<ShowModel::ActionHit> hit = ShowModel::actionAt(m_deck->slides.at(m_slide), QPointF(x, y), m_build, false);
    return hit.has_value() && runAction(hit->action, hit->bounds);
}

bool PresentationController::hover(qreal x, qreal y)
{
    if (m_slide < 0 || m_slide >= m_deck->slides.size()) {
        return false;
    }
    const ShowModel::Slide &slide = m_deck->slides.at(m_slide);
    const std::optional<ShowModel::ActionHit> over = ShowModel::actionAt(slide, QPointF(x, y), m_build, true);
    const bool clickable = ShowModel::actionAt(slide, QPointF(x, y), m_build, false).has_value();
    const QString shape = over ? over->shapeId : QString();
    if (shape != m_hoverShape) {
        m_hoverShape = shape;
        if (over) {
            runAction(over->action, over->bounds);
        }
    }
    return over.has_value() || clickable;
}

void PresentationController::playCustomShow(quint32 number, bool returnAfter)
{
    const int index = ShowModel::customShowIndex(*m_deck, number);
    const QList<int> order = index >= 0 ? ShowModel::showPlaylist(*m_deck, index) : QList<int>();
    if (order.isEmpty()) {
        emit noticeRequested(tr("The custom show this link names has no slides to show"));
        return;
    }
    if (returnAfter) {
        m_returns.append({m_playlist, m_position, m_slide, m_build, m_showName});
    }
    m_playlist = order;
    m_showName = m_deck->customShows.at(index).name;
    moveTo(0, 0);
    emit stateChanged();
}

bool PresentationController::runAction(const ShowModel::ShowAction &action, const QRectF &bounds)
{
    using ShowModel::ActionKind;
    if (action.isEmpty()) {
        return false;
    }
    if (action.stopSound) {
        emit stopSoundsRequested();
    }
    if (!action.sound.isEmpty() && m_soundResolver) {
        emit soundRequested(m_soundResolver(action.sound, action.soundName));
    }
    if (action.highlight && bounds.isValid()) {
        emit highlightRequested(bounds);
    }
    const ShowModel::ActionTarget target = ShowModel::parseAction(action);
    switch (target.kind) {
    case ActionKind::NextSlide:
        goNext();
        break;
    case ActionKind::PreviousSlide:
        if (const int previous = previousPosition(); previous >= 0) {
            moveTo(previous, 0);
            emit stateChanged();
        }
        break;
    case ActionKind::FirstSlide:
    case ActionKind::LastSlide:
        if (!m_playlist.isEmpty()) {
            moveTo(target.kind == ActionKind::FirstSlide ? 0 : int(m_playlist.size()) - 1, 0);
            emit stateChanged();
        }
        break;
    case ActionKind::LastViewed:
        for (qsizetype i = m_history.size() - 2; i >= 0; --i) {
            if (m_history.at(i) != m_slide) {
                goTo(m_history.at(i), 0);
                break;
            }
        }
        break;
    case ActionKind::EndShow:
        emit endRequested();
        break;
    case ActionKind::Slide:
        for (qsizetype i = 0; i < m_deck->slides.size(); ++i) {
            if (m_deck->slides.at(i).id == target.slideId) {
                goTo(int(i), 0); // a hidden slide too: a link shows it
                break;
            }
        }
        break;
    case ActionKind::CustomShow:
        playCustomShow(target.customShow, target.returnAfter);
        break;
    case ActionKind::Url:
        emit openRequested(target.address, false);
        break;
    case ActionKind::File:
    case ActionKind::Presentation:
        emit openRequested(target.address, true);
        break;
    case ActionKind::Program:
        emit noticeRequested(tr("This slide asks to run a program (%1). QindaShow never runs programs.")
                                 .arg(target.address.isEmpty() ? tr("none named") : target.address));
        break;
    case ActionKind::Macro:
        emit noticeRequested(tr("This slide asks to run the macro “%1”. QindaShow does not run macros.").arg(target.address));
        break;
    case ActionKind::Media:
        return false; // the player plays or pauses the medium itself
    case ActionKind::None:
    case ActionKind::Other:
        break;
    }
    return true;
}

} // namespace QindaOffice::Apps::Show
