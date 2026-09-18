// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_arrange.h"

#include "show_commands.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"
#include "standard_actions.h"

#include <QUndoStack>
#include <QUuid>

#include <algorithm>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;

ShowArrangeActions::ShowArrangeActions(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, [this] { syncFromSelection(); });
    syncFromSelection();
}

void ShowArrangeActions::createActions()
{
    using namespace Quick::ActionId;
    Quick::ActionModel *model = m_host->actions();
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    m_bringToFront = make(kShowBringToFront, tr("Bring to &Front"), QKeySequence(QStringLiteral("Ctrl+Shift+]")),
                          "bring-to-front");
    m_bringForward = make(kShowBringForward, tr("Bring F&orward"), QKeySequence(QStringLiteral("Ctrl+]")),
                          "chevrons-up");
    m_sendBackward = make(kShowSendBackward, tr("Send &Backward"), QKeySequence(QStringLiteral("Ctrl+[")),
                          "chevrons-down");
    m_sendToBack = make(kShowSendToBack, tr("Send to &Back"), QKeySequence(QStringLiteral("Ctrl+Shift+[")),
                        "send-to-back");
    m_duplicate = make(kShowDuplicateShape, tr("&Duplicate"), QKeySequence(QStringLiteral("Ctrl+D")), "copy");
    m_deleteShape = make(kShowDeleteShape, tr("De&lete Shape"), {}, "trash-2");
    m_alignLeft = make(kShowAlignLeft, tr("Align &Left"), {}, "align-start-vertical");
    m_alignHCenter = make(kShowAlignHCenter, tr("Align &Center"), {}, "align-center-vertical");
    m_alignRight = make(kShowAlignRight, tr("Align &Right"), {}, "align-end-vertical");
    m_alignTop = make(kShowAlignTop, tr("Align &Top"), {}, "align-start-horizontal");
    m_alignVCenter = make(kShowAlignVCenter, tr("Align &Middle"), {}, "align-center-horizontal");
    m_alignBottom = make(kShowAlignBottom, tr("Align &Bottom"), {}, "align-end-horizontal");
    m_distributeH = make(kShowDistributeH, tr("Distribute &Horizontally"), {}, "align-horizontal-space-between");
    m_distributeV = make(kShowDistributeV, tr("Distribute &Vertically"), {}, "align-vertical-space-between");
    m_slideMoveUp = make(kShowSlideMoveUp, tr("Move Slide &Up"), QKeySequence(QStringLiteral("Ctrl+Shift+Up")),
                         "arrow-up");
    m_slideMoveDown = make(kShowSlideMoveDown, tr("Move Slide &Down"),
                           QKeySequence(QStringLiteral("Ctrl+Shift+Down")), "arrow-down");

    SlideCanvasItem *canvas = m_host->canvas();
    connect(m_bringToFront, &ActionEntry::triggered, canvas, &SlideCanvasItem::bringSelectedToFront);
    connect(m_bringForward, &ActionEntry::triggered, canvas, &SlideCanvasItem::raiseSelected);
    connect(m_sendBackward, &ActionEntry::triggered, canvas, &SlideCanvasItem::lowerSelected);
    connect(m_sendToBack, &ActionEntry::triggered, canvas, &SlideCanvasItem::sendSelectedToBack);
    connect(m_duplicate, &ActionEntry::triggered, this, [this] { duplicateSelected(); });
    connect(m_deleteShape, &ActionEntry::triggered, canvas, &SlideCanvasItem::deleteSelected);
    connect(m_alignLeft, &ActionEntry::triggered, this, [this] { applyAlign(Align::Left); });
    connect(m_alignHCenter, &ActionEntry::triggered, this, [this] { applyAlign(Align::HCenter); });
    connect(m_alignRight, &ActionEntry::triggered, this, [this] { applyAlign(Align::Right); });
    connect(m_alignTop, &ActionEntry::triggered, this, [this] { applyAlign(Align::Top); });
    connect(m_alignVCenter, &ActionEntry::triggered, this, [this] { applyAlign(Align::VCenter); });
    connect(m_alignBottom, &ActionEntry::triggered, this, [this] { applyAlign(Align::Bottom); });
    connect(m_distributeH, &ActionEntry::triggered, this, [this] { distribute(true); });
    connect(m_distributeV, &ActionEntry::triggered, this, [this] { distribute(false); });
    connect(m_slideMoveUp, &ActionEntry::triggered, this, [this] { moveSlide(-1); });
    connect(m_slideMoveDown, &ActionEntry::triggered, this, [this] { moveSlide(1); });
}

void ShowArrangeActions::syncFromSelection()
{
    SlideCanvasItem *canvas = m_host->canvas();
    const int selected = canvas->slide() != nullptr ? canvas->selectionCount() : 0;
    for (ActionEntry *entry : {m_bringToFront, m_bringForward, m_sendBackward, m_sendToBack, m_duplicate,
                               m_deleteShape, m_alignLeft, m_alignHCenter, m_alignRight, m_alignTop,
                               m_alignVCenter, m_alignBottom}) {
        entry->setEnabled(selected > 0);
    }
    // Distributing fewer than three shapes is a no-op: the outer two define
    // the span and there is nothing in between to space out.
    m_distributeH->setEnabled(selected >= 3);
    m_distributeV->setEnabled(selected >= 3);
    const int row = m_host->currentSlideRow();
    const int count = int(m_host->showDocument()->presentation()->slides.size());
    m_slideMoveUp->setEnabled(row > 0);
    m_slideMoveDown->setEnabled(row >= 0 && row + 1 < count);
}

void ShowArrangeActions::asOneSlideEdit(const QString &text, const std::function<void()> &mutate)
{
    ShowModel::Slide *slide = m_host->canvas()->slide();
    if (slide == nullptr) {
        return;
    }
    const ShowModel::Slide before = *slide;
    mutate();
    if (*slide == before) {
        return;
    }
    auto *command = new SlideStateCommand(m_host->showDocument()->presentation(), slide->id, before, *slide);
    command->setText(text);
    m_host->showDocument()->undoStack()->push(command);
}

void ShowArrangeActions::applyAlign(Align align)
{
    SlideCanvasItem *canvas = m_host->canvas();
    ShowModel::Slide *slide = canvas->slide();
    const QStringList ids = canvas->selectedShapeIds();
    if (slide == nullptr || ids.isEmpty()) {
        return;
    }
    // One shape aligns to the SLIDE; several align to each other. That is
    // what makes "align center" useful in both situations without a mode.
    QRectF frame;
    if (ids.size() == 1) {
        frame = QRectF(QPointF(0, 0), canvas->slideSize());
    } else {
        for (const QString &id : ids) {
            if (const ShowModel::Shape *shape = slide->findShape(id)) {
                frame = frame.isNull() ? shape->rect.normalized() : frame.united(shape->rect.normalized());
            }
        }
    }
    QHash<QString, QRectF> rects;
    for (const QString &id : ids) {
        const ShowModel::Shape *shape = slide->findShape(id);
        if (shape == nullptr) {
            continue;
        }
        QRectF rect = shape->rect.normalized();
        switch (align) {
        case Align::Left: rect.moveLeft(frame.left()); break;
        case Align::HCenter: rect.moveLeft(frame.center().x() - rect.width() / 2.0); break;
        case Align::Right: rect.moveRight(frame.right()); break;
        case Align::Top: rect.moveTop(frame.top()); break;
        case Align::VCenter: rect.moveTop(frame.center().y() - rect.height() / 2.0); break;
        case Align::Bottom: rect.moveBottom(frame.bottom()); break;
        }
        rects.insert(id, rect);
    }
    asOneSlideEdit(tr("Align"), [canvas, rects] { canvas->setShapeRects(rects); });
}

void ShowArrangeActions::distribute(bool horizontal)
{
    SlideCanvasItem *canvas = m_host->canvas();
    ShowModel::Slide *slide = canvas->slide();
    QStringList ids = canvas->selectedShapeIds();
    if (slide == nullptr || ids.size() < 3) {
        return;
    }
    // Sort by current centre, so the shapes keep their visual order and only
    // the gaps between them change.
    std::sort(ids.begin(), ids.end(), [slide, horizontal](const QString &a, const QString &b) {
        const ShowModel::Shape *sa = slide->findShape(a);
        const ShowModel::Shape *sb = slide->findShape(b);
        if (sa == nullptr || sb == nullptr) {
            return false;
        }
        return horizontal ? sa->rect.center().x() < sb->rect.center().x()
                          : sa->rect.center().y() < sb->rect.center().y();
    });
    const ShowModel::Shape *first = slide->findShape(ids.first());
    const ShowModel::Shape *last = slide->findShape(ids.last());
    if (first == nullptr || last == nullptr) {
        return;
    }
    const qreal start = horizontal ? first->rect.normalized().center().x() : first->rect.normalized().center().y();
    const qreal end = horizontal ? last->rect.normalized().center().x() : last->rect.normalized().center().y();
    const qreal step = (end - start) / qreal(ids.size() - 1);
    QHash<QString, QRectF> rects;
    for (int i = 1; i < ids.size() - 1; ++i) {
        const ShowModel::Shape *shape = slide->findShape(ids.at(i));
        if (shape == nullptr) {
            continue;
        }
        QRectF rect = shape->rect.normalized();
        const qreal target = start + step * qreal(i);
        if (horizontal) {
            rect.moveLeft(target - rect.width() / 2.0);
        } else {
            rect.moveTop(target - rect.height() / 2.0);
        }
        rects.insert(ids.at(i), rect);
    }
    asOneSlideEdit(tr("Distribute"), [canvas, rects] { canvas->setShapeRects(rects); });
}

void ShowArrangeActions::duplicateSelected()
{
    SlideCanvasItem *canvas = m_host->canvas();
    ShowModel::Slide *slide = canvas->slide();
    const QStringList ids = canvas->selectedShapeIds();
    if (slide == nullptr || ids.isEmpty()) {
        return;
    }
    asOneSlideEdit(tr("Duplicate shape"), [canvas, slide, ids] {
        QStringList created;
        for (const QString &id : ids) {
            const ShowModel::Shape *source = slide->findShape(id);
            if (source == nullptr) {
                continue;
            }
            ShowModel::Shape copy = *source;
            copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            // Offset so the copy is visibly a second object, the same
            // +12 pt the shape clipboard uses.
            copy.rect.translate(12, 12);
            slide->shapes.append(copy);
            created.append(copy.id);
        }
        canvas->clearSelection();
        for (const QString &id : created) {
            canvas->setSelected(id, true);
        }
    });
}

void ShowArrangeActions::moveSlide(int delta)
{
    ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const int row = m_host->currentSlideRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= deck->slides.size()) {
        return;
    }
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.move(row, target);
    auto *command = new SlideListCommand(deck, before, deck->slides);
    command->setText(tr("Move slide"));
    m_host->showDocument()->undoStack()->push(command);
    m_host->selectSlideRow(target);
}

} // namespace QindaOffice::Apps::Show
