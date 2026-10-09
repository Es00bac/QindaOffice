// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_items.h"

#include "presentation_controller.h"
#include "show_document.h"
#include "show_host.h"
#include "show_master_view.h"
#include "show_slide_layouts.h"
#include "slide_renderer.h"

#include <QPainter>

namespace QindaOffice::Apps::Show {

namespace {

// Letterboxes `slideSize` into `target` and returns the scale applied.
qreal letterbox(QPainter *painter, const QRectF &target, const QSizeF &slideSize)
{
    const qreal scale = qMin(target.width() / slideSize.width(), target.height() / slideSize.height());
    painter->translate(target.center() - QPointF(slideSize.width() * scale / 2.0, slideSize.height() * scale / 2.0));
    painter->scale(scale, scale);
    return scale;
}

} // namespace

// ---- SlideThumbItem ------------------------------------------------------------------

SlideThumbItem::SlideThumbItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setImplicitWidth(160);
    setImplicitHeight(90);
}

void SlideThumbItem::setHost(ShowHost *host)
{
    if (m_host == host) {
        return;
    }
    m_host = host;
    emit changed();
    update();
}

void SlideThumbItem::setSlideIndex(int index)
{
    if (m_slideIndex == index) {
        return;
    }
    m_slideIndex = index;
    emit changed();
    update();
}

void SlideThumbItem::setRevision(int revision)
{
    if (m_revision == revision) {
        return;
    }
    m_revision = revision;
    emit changed();
    update();
}

void SlideThumbItem::setTemplateIndex(int index)
{
    if (m_templateIndex == index) {
        return;
    }
    m_templateIndex = index;
    emit changed();
    update();
}

void SlideThumbItem::setGalleryIndex(int index)
{
    if (m_galleryIndex != index) {
        m_galleryIndex = index;
        emit changed();
        update();
    }
}

void SlideThumbItem::setReuseIndex(int index)
{
    if (m_reuseIndex != index) {
        m_reuseIndex = index;
        emit changed();
        update();
    }
}

void SlideThumbItem::paint(QPainter *painter)
{
    const ShowModel::Presentation *deck = m_host != nullptr ? m_host->showDocument()->presentation() : nullptr;
    // M82: a layout of the New Slide gallery, or a slide Reuse Slides offers.
    if (deck != nullptr && (m_galleryIndex >= 0 || m_reuseIndex >= 0)) {
        const ShowModel::Presentation *reuse = m_host->slideLayouts()->reuseDeck();
        const ShowModel::Presentation *shown = m_reuseIndex >= 0 ? reuse : deck;
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        if (shown == nullptr || (m_reuseIndex >= 0 && m_reuseIndex >= shown->slides.size())) {
            painter->fillRect(boundingRect(), Qt::white);
            return;
        }
        painter->save();
        letterbox(painter, boundingRect(), shown->slideSize);
        if (m_reuseIndex >= 0) {
            ShowModel::renderSlide(painter, *shown, shown->slides.at(m_reuseIndex));
        } else {
            m_host->slideLayouts()->renderEntry(m_galleryIndex, painter, shown->slideSize);
        }
        painter->restore();
        return;
    }
    if (deck != nullptr && m_templateIndex >= 0) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->save();
        letterbox(painter, boundingRect(), deck->slideSize);
        m_host->masterView()->renderItem(m_templateIndex, painter, deck->slideSize);
        painter->restore();
        return;
    }
    if (deck == nullptr || m_slideIndex < 0 || m_slideIndex >= deck->slides.size()) {
        painter->fillRect(boundingRect(), Qt::white);
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const ShowModel::Slide &slide = deck->slides.at(m_slideIndex);
    painter->save();
    letterbox(painter, boundingRect(), deck->slideSize);
    ShowModel::renderSlide(painter, *deck, slide);
    painter->restore();
}

// ---- SlideFrameItem ------------------------------------------------------------------

SlideFrameItem::SlideFrameItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::black);
}

void SlideFrameItem::setController(PresentationController *controller)
{
    if (m_controller == controller) {
        return;
    }
    if (m_controller != nullptr) {
        disconnect(m_controller, nullptr, this, nullptr);
    }
    m_controller = controller;
    if (controller != nullptr) {
        connect(controller, &PresentationController::stateChanged, this, [this] {
            if (!m_frozen) {
                update();
            }
        });
    }
    emit changed();
    update();
}

void SlideFrameItem::setSlideIndex(int index)
{
    if (m_slideIndex == index) {
        return;
    }
    m_slideIndex = index;
    emit changed();
    update();
}

void SlideFrameItem::setBuildStep(int step)
{
    if (m_buildStep == step) {
        return;
    }
    m_buildStep = step;
    emit changed();
    update();
}

void SlideFrameItem::setBuildProgress(qreal progress)
{
    const qreal bounded = qBound<qreal>(0.0, progress, 1.0);
    if (qFuzzyCompare(m_buildProgress, bounded)) {
        return;
    }
    m_buildProgress = bounded;
    emit changed();
    update();
}

void SlideFrameItem::setFrozen(bool frozen)
{
    if (m_frozen == frozen) {
        return;
    }
    m_frozen = frozen;
    emit changed();
    update();
}

int SlideFrameItem::effectiveSlide() const
{
    if (m_slideIndex >= 0) {
        return m_slideIndex;
    }
    // -1 follows the show; any other negative index is "no slide" (the
    // presenter's next preview after the last slide, M67).
    return m_slideIndex == -1 && m_controller != nullptr ? m_controller->slide() : -1;
}

int SlideFrameItem::effectiveBuild() const
{
    if (m_buildStep >= 0) {
        return m_buildStep;
    }
    return m_controller != nullptr ? m_controller->build() : 1000;
}

void SlideFrameItem::freezeCurrent()
{
    if (m_controller == nullptr) {
        return;
    }
    m_slideIndex = m_controller->slide();
    m_buildStep = m_controller->build();
    m_buildProgress = 1.0;
    m_frozen = true;
    emit changed();
    update();
}

void SlideFrameItem::paint(QPainter *painter)
{
    const ShowModel::Presentation *deck = m_controller != nullptr ? m_controller->deck() : nullptr;
    const int slideIndex = effectiveSlide();
    if (deck == nullptr || slideIndex < 0 || slideIndex >= deck->slides.size()) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const ShowModel::Slide &slide = deck->slides.at(slideIndex);
    painter->save();
    letterbox(painter, boundingRect(), deck->slideSize);
    ShowModel::renderSlide(painter, *deck, slide, effectiveBuild(), m_buildProgress);
    painter->restore();
}

} // namespace QindaOffice::Apps::Show
