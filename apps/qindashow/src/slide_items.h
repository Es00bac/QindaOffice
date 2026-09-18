// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Two small painted items over show_model's renderer (conversion plan
// §5.3), so the sorter, the player and the presenter show exactly what
// the canvas and the PDF show:
//  - SlideThumbItem paints slide `slideIndex` of the host's deck, small,
//    with its master; the sorter's Repeater makes one per slide and they
//    repaint on the host's deckRevision.
//  - SlideFrameItem paints a slide of a PresentationController's deck
//    letterboxed on black at a build step and build progress — the
//    player's two frames (outgoing/incoming) and the presenter's current
//    and next previews are all this item.

#include "presentation.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

class PresentationController;
class ShowHost;

class SlideThumbItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ShowHost *host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(int slideIndex READ slideIndex WRITE setSlideIndex NOTIFY changed)
    // Bind to the host's deckRevision: any bump repaints.
    Q_PROPERTY(int revision READ revision WRITE setRevision NOTIFY changed)
public:
    explicit SlideThumbItem(QQuickItem *parent = nullptr);
    [[nodiscard]] ShowHost *host() const { return m_host; }
    void setHost(ShowHost *host);
    [[nodiscard]] int slideIndex() const { return m_slideIndex; }
    void setSlideIndex(int index);
    [[nodiscard]] int revision() const { return m_revision; }
    void setRevision(int revision);
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    QPointer<ShowHost> m_host;
    int m_slideIndex = -1;
    int m_revision = 0;
};

class SlideFrameItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PresentationController *controller READ controller WRITE setController NOTIFY changed)
    // -1 follows the controller's current slide and build step.
    Q_PROPERTY(int slideIndex READ slideIndex WRITE setSlideIndex NOTIFY changed)
    Q_PROPERTY(int buildStep READ buildStep WRITE setBuildStep NOTIFY changed)
    Q_PROPERTY(qreal buildProgress READ buildProgress WRITE setBuildProgress NOTIFY changed)
    // Set by the player when it snapshots the outgoing frame: the item
    // stops following the controller until told otherwise.
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY changed)
public:
    explicit SlideFrameItem(QQuickItem *parent = nullptr);
    [[nodiscard]] PresentationController *controller() const { return m_controller; }
    void setController(PresentationController *controller);
    [[nodiscard]] int slideIndex() const { return m_slideIndex; }
    void setSlideIndex(int index);
    [[nodiscard]] int buildStep() const { return m_buildStep; }
    void setBuildStep(int step);
    [[nodiscard]] qreal buildProgress() const { return m_buildProgress; }
    void setBuildProgress(qreal progress);
    [[nodiscard]] bool frozen() const { return m_frozen; }
    void setFrozen(bool frozen);
    // The slide/build actually painted (the controller's when following).
    [[nodiscard]] int effectiveSlide() const;
    [[nodiscard]] int effectiveBuild() const;
    // Snapshot the current state so a transition can paint the old frame.
    Q_INVOKABLE void freezeCurrent();
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    QPointer<PresentationController> m_controller;
    int m_slideIndex = -1;
    int m_buildStep = -1;
    qreal m_buildProgress = 1.0;
    bool m_frozen = false;
};

} // namespace QindaOffice::Apps::Show
