// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation_controller.h"

namespace QindaOffice::Apps::Show {

PresentationController::PresentationController(ShowModel::Presentation *deck, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
{
}

int PresentationController::buildCap(int slideIndex) const
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
    }
    return longest;
}

bool PresentationController::atEnd() const
{
    return m_slide >= m_deck->slides.size() - 1 && m_build >= buildCap(m_slide);
}

void PresentationController::goTo(int slide, int build)
{
    m_slide = qBound(0, slide, qMax(0, m_deck->slides.size() - 1));
    m_build = qBound(0, build, buildCap(m_slide));
    emit stateChanged();
}

void PresentationController::advance(int delta)
{
    if (delta > 0) {
        if (m_build < buildCap(m_slide)) {
            ++m_build;
        } else if (m_slide < m_deck->slides.size() - 1) {
            ++m_slide;
            m_build = 0;
        } else {
            return; // past the last slide is a no-op
        }
    } else if (delta < 0) {
        if (m_build > 0) {
            --m_build;
        } else if (m_slide > 0) {
            --m_slide;
            m_build = buildCap(m_slide); // fully revealed
        } else {
            return;
        }
    } else {
        return;
    }
    emit stateChanged();
}

} // namespace QindaOffice::Apps::Show
