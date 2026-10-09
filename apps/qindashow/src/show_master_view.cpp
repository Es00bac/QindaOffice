// SPDX-License-Identifier: GPL-3.0-or-later
// The Slide Master view's state, canvas, thumbnails and text styles
// (show_master_view.h); its commands are show_master_actions.cpp.

#include "show_master_view.h"

#include "pptx_design.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QPainter>
#include <QTimer>

namespace QindaOffice::Apps::Show {

namespace {

QString alignmentName(int alignment)
{
    if (alignment & int(Qt::AlignHCenter)) {
        return QStringLiteral("center");
    }
    if (alignment & int(Qt::AlignJustify)) {
        return QStringLiteral("justify");
    }
    return alignment & int(Qt::AlignRight) ? QStringLiteral("right") : QStringLiteral("left");
}

int alignmentValue(const QString &name)
{
    if (name == u"center") {
        return int(Qt::AlignHCenter);
    }
    if (name == u"justify") {
        return int(Qt::AlignJustify);
    }
    return name == u"right" ? int(Qt::AlignRight) : int(Qt::AlignLeft);
}

ShowModel::SlideBackdrop backdropOf(const ShowModel::Presentation &deck, const QString &masterId, const QString &layoutId)
{
    const ShowModel::SlideMaster *master = deck.findMaster(masterId);
    ShowModel::SlideBackdrop backdrop = ShowModel::backdropFor(master);
    const ShowModel::SlideLayout *layout = layoutId.isEmpty() ? nullptr : deck.findLayout(layoutId);
    if (layout == nullptr) {
        backdrop.shapes.clear(); // the master's decorations are the slide being edited
        return backdrop;
    }
    if (!layout->showMasterShapes) {
        backdrop.shapes.clear();
    }
    if (layout->background.set) {
        backdrop.color = layout->background.color;
        backdrop.style = layout->background.style;
    }
    return backdrop;
}

} // namespace

ShowMasterView::ShowMasterView(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    // AGENT-GUARD (show_master_view.h): commits are queued, never run inside
    // the canvas's own signal.
    connect(host->canvas(), &SlideCanvasItem::slideEdited, this, [this] {
        if (m_active) {
            scheduleCommit();
        }
    });
    connect(host->canvas(), &SlideCanvasItem::shapeEdited, this, [this] {
        if (m_active) {
            scheduleCommit();
        }
    });
    connect(host, &ShowHost::deckChanged, this, [this] {
        ++m_revision;
        emit itemsChanged();
        syncActions();
    });
}

QList<ShowMasterView::Item> ShowMasterView::listed() const
{
    QList<Item> out;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (const ShowModel::SlideMaster &master : deck->masters) {
        out.append({master.id, QString()});
        for (const ShowModel::SlideLayout *layout : ShowModel::layoutsOfMaster(*deck, master.id)) {
            out.append({master.id, layout->id});
        }
    }
    return out;
}

QVariantList ShowMasterView::items() const
{
    QVariantList list;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (const Item &item : listed()) {
        const ShowModel::SlideLayout *layout = item.layoutId.isEmpty() ? nullptr : deck->findLayout(item.layoutId);
        const ShowModel::SlideMaster *master = deck->findMaster(item.masterId);
        int uses = 0;
        for (const ShowModel::Slide &slide : deck->slides) {
            uses += (layout != nullptr ? slide.layoutId == layout->id : slide.master == item.masterId) ? 1 : 0;
        }
        list.append(QVariantMap{{QStringLiteral("masterId"), item.masterId},
                                {QStringLiteral("layoutId"), item.layoutId},
                                {QStringLiteral("name"), layout != nullptr ? layout->name
                                                         : master != nullptr && !master->name.isEmpty() ? master->name
                                                                                                         : tr("Slide Master")},
                                {QStringLiteral("master"), layout == nullptr},
                                {QStringLiteral("uses"), uses},
                                {QStringLiteral("preserve"), layout != nullptr && layout->preserve}});
    }
    return list;
}

int ShowMasterView::currentIndex() const
{
    const QList<Item> all = listed();
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (all.at(i).masterId == m_masterId && all.at(i).layoutId == m_layoutId) {
            return int(i);
        }
    }
    return -1;
}

void ShowMasterView::setCurrentIndex(int index)
{
    const QList<Item> all = listed();
    if (index >= 0 && index < all.size() && index != currentIndex()) {
        select(all.at(index).masterId, all.at(index).layoutId);
    }
}

QString ShowMasterView::currentName() const
{
    const int index = currentIndex();
    const QVariantList all = items();
    return index >= 0 ? all.at(index).toMap().value(QStringLiteral("name")).toString() : QString();
}

QString ShowMasterView::currentMasterId() const
{
    if (m_active) {
        return m_masterId;
    }
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide != nullptr ? slide->master : QString();
}

void ShowMasterView::select(const QString &masterId, const QString &layoutId)
{
    if (m_commitPending) {
        commitCanvas();
    }
    m_masterId = masterId;
    m_layoutId = layoutId;
    m_host->canvas()->clearSelection();
    refreshCanvas();
    emit currentChanged();
}

void ShowMasterView::enter()
{
    if (m_active) {
        return;
    }
    m_host->canvas()->commitTextEdit(false);
    // A deck without PowerPoint's structure gets it first: a master with
    // its theme, placeholders and text styles, and layouts.
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const bool structured = !deck->masters.isEmpty()
        && std::all_of(deck->masters.cbegin(), deck->masters.cend(), [](const ShowModel::SlideMaster &master) {
               return !master.placeholders.isEmpty() && !master.titleStyle.isEmpty() && !master.theme.isEmpty();
           });
    if (!structured) {
        QString error;
        const ShowModel::Presentation after = ShowModel::reresolved(*deck, &error);
        if (!error.isEmpty()) {
            m_host->showStatusMessage(tr("The slide master could not be opened: %1").arg(error), 8000);
            syncActions();
            return;
        }
        m_host->applyDeckChange(after, tr("Slide master"));
        deck = m_host->showDocument()->presentation();
    }
    const ShowModel::Slide *slide = m_host->currentSlide();
    m_masterId = slide != nullptr && deck->findMaster(slide->master) != nullptr ? slide->master
        : deck->masters.isEmpty()                                            ? QString()
                                                                              : deck->masters.first().id;
    m_layoutId = slide != nullptr && deck->findLayout(slide->layoutId) != nullptr ? slide->layoutId : QString();
    m_active = true;
    m_host->canvas()->clearSelection();
    emit activeChanged();
    refreshCanvas();
    emit currentChanged();
}

void ShowMasterView::close()
{
    if (!m_active) {
        return;
    }
    m_host->canvas()->commitTextEdit(false);
    if (m_commitPending) {
        commitCanvas();
    }
    m_active = false;
    m_host->canvas()->clearSelection();
    m_host->canvas()->setTemplateMode(false);
    emit activeChanged();
    m_host->refreshSlides();
    syncActions();
}

ShowModel::SlideBackdrop ShowMasterView::backdrop() const
{
    return backdropOf(*m_host->showDocument()->presentation(), m_masterId, m_layoutId);
}

void ShowMasterView::refreshCanvas()
{
    if (!m_active) {
        return;
    }
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (deck->findMaster(m_masterId) == nullptr) {
        m_masterId = deck->masters.isEmpty() ? QString() : deck->masters.first().id;
        m_layoutId.clear();
    }
    if (!m_layoutId.isEmpty() && deck->findLayout(m_layoutId) == nullptr) {
        m_layoutId.clear(); // gone (undone, deleted): its master
    }
    // The selection, by Office id, across the re-resolution.
    SlideCanvasItem *canvas = m_host->canvas();
    QSet<quint32> selected;
    for (const QString &id : canvas->selectedShapeIds()) {
        if (const ShowModel::Shape *shape = m_template.findShape(id); shape != nullptr && shape->nvId != 0) {
            selected.insert(shape->nvId);
        }
    }
    m_template = ShowModel::templateSlide(*deck, m_masterId, m_layoutId);
    canvas->setTemplateMode(true);
    canvas->setBackdrop(backdrop(), deck->findMaster(m_masterId));
    canvas->setSlide(&m_template, deck->slideSize);
    for (const ShowModel::Shape &shape : std::as_const(m_template.shapes)) {
        if (selected.contains(shape.nvId)) {
            canvas->setSelected(shape.id, true);
        }
    }
    ++m_revision;
    emit itemsChanged();
    syncActions();
}

void ShowMasterView::renderItem(int index, QPainter *painter, const QSizeF &size) const
{
    const QList<Item> all = listed();
    if (index < 0 || index >= all.size()) {
        return;
    }
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const Item &item = all.at(index);
    const ShowModel::Slide slide = ShowModel::templateSlide(*deck, item.masterId, item.layoutId);
    painter->save();
    painter->scale(size.width() / deck->slideSize.width(), size.height() / deck->slideSize.height());
    ShowModel::renderSlide(painter, slide, deck->slideSize, backdropOf(*deck, item.masterId, item.layoutId));
    // Placeholders show their frames, as in PowerPoint's master view.
    QPen frame(QColor(140, 140, 140), 0);
    frame.setStyle(Qt::DashLine);
    painter->setPen(frame);
    painter->setBrush(Qt::NoBrush);
    for (const ShowModel::Shape &shape : slide.shapes) {
        if (shape.placeholder.isPlaceholder()) {
            painter->drawRect(shape.rect.normalized());
        }
    }
    painter->restore();
}

void ShowMasterView::scheduleCommit()
{
    if (m_commitPending) {
        return;
    }
    m_commitPending = true;
    QTimer::singleShot(0, this, [this] {
        if (m_commitPending) {
            commitCanvas();
        }
    });
}

void ShowMasterView::commitCanvas()
{
    m_commitPending = false;
    if (!m_active) {
        return;
    }
    const QString label = m_layoutId.isEmpty() ? tr("Edit slide master") : tr("Edit layout");
    const ShowModel::Slide edited = m_template;
    if (!change(label, [&](ShowModel::Presentation &deck) {
            return ShowModel::applyTemplateSlide(deck, m_masterId, m_layoutId, edited);
        })) {
        refreshCanvas();
    }
}

bool ShowMasterView::change(const QString &label, const std::function<bool(ShowModel::Presentation &)> &mutate)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (!mutate(after)) {
        return false;
    }
    QString error;
    after = ShowModel::reresolved(after, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The change could not be made: %1").arg(error), 8000);
        return false;
    }
    return m_host->applyDeckChange(after, label);
}

QStringList ShowMasterView::themeFonts() const
{
    const ShowModel::ThemeSpec theme = ShowModel::deckTheme(*m_host->showDocument()->presentation(), currentMasterId());
    return {theme.fonts.major, theme.fonts.minor};
}

QVariantList ShowMasterView::styleLevels() const
{
    QVariantList rows;
    const ShowModel::SlideMaster *master = m_host->showDocument()->presentation()->findMaster(currentMasterId());
    if (master == nullptr) {
        return rows;
    }
    for (int row = 0; row < 6; ++row) {
        const ShowModel::TextParagraph *level = ShowModel::styleLevel(*master, row == 0 ? ShowModel::StyleList::Title
                                                                                         : ShowModel::StyleList::Body,
                                                                      row == 0 ? 0 : row - 1);
        if (level == nullptr) {
            continue;
        }
        const ShowModel::TextRun run = level->runs.isEmpty() ? ShowModel::TextRun() : level->runs.first();
        rows.append(QVariantMap{{QStringLiteral("row"), row},
                                {QStringLiteral("label"), row == 0 ? tr("Title") : tr("Text level %1").arg(row)},
                                {QStringLiteral("family"), run.fontFamily},
                                {QStringLiteral("size"), run.pointSize},
                                {QStringLiteral("bold"), run.bold},
                                {QStringLiteral("italic"), run.italic},
                                {QStringLiteral("underline"), !run.underline.isEmpty()},
                                {QStringLiteral("color"), run.color},
                                {QStringLiteral("bullet"), level->bullet ? (level->autoNumber.isEmpty() ? (level->bulletChar.isEmpty() ? QStringLiteral("•") : level->bulletChar) : QStringLiteral("1."))
                                                                         : QString()},
                                {QStringLiteral("alignment"), alignmentName(level->alignment)},
                                {QStringLiteral("indent"), level->marginLeft}});
    }
    return rows;
}

void ShowMasterView::setStyleLevel(int row, const QVariantMap &look)
{
    const ShowModel::SlideMaster *master = m_host->showDocument()->presentation()->findMaster(currentMasterId());
    const ShowModel::StyleList list = row == 0 ? ShowModel::StyleList::Title : ShowModel::StyleList::Body;
    const int level = row == 0 ? 0 : row - 1;
    const ShowModel::TextParagraph *now = master != nullptr ? ShowModel::styleLevel(*master, list, level) : nullptr;
    if (now == nullptr) {
        return;
    }
    ShowModel::TextParagraph next = *now;
    ShowModel::TextRun run = next.runs.isEmpty() ? ShowModel::TextRun() : next.runs.first();
    if (look.contains(QStringLiteral("family"))) {
        run.fontFamily = look.value(QStringLiteral("family")).toString();
    }
    if (look.contains(QStringLiteral("size"))) {
        run.pointSize = qBound(1.0, look.value(QStringLiteral("size")).toDouble(), 400.0);
    }
    if (look.contains(QStringLiteral("bold"))) {
        run.bold = look.value(QStringLiteral("bold")).toBool();
    }
    if (look.contains(QStringLiteral("italic"))) {
        run.italic = look.value(QStringLiteral("italic")).toBool();
    }
    if (look.contains(QStringLiteral("underline"))) {
        run.underline = look.value(QStringLiteral("underline")).toBool() ? QStringLiteral("sng") : QString();
    }
    if (look.contains(QStringLiteral("color")) && look.value(QStringLiteral("color")).value<QColor>().isValid()) {
        run.color = look.value(QStringLiteral("color")).value<QColor>();
    }
    if (look.contains(QStringLiteral("bullet"))) {
        const QString bullet = look.value(QStringLiteral("bullet")).toString();
        next.bullet = !bullet.isEmpty();
        next.autoNumber = bullet == u"1." ? QStringLiteral("arabicPeriod") : QString();
        next.bulletChar = next.bullet && next.autoNumber.isEmpty() ? bullet : QString();
    }
    if (look.contains(QStringLiteral("alignment"))) {
        next.alignment = alignmentValue(look.value(QStringLiteral("alignment")).toString());
    }
    if (look.contains(QStringLiteral("indent"))) {
        next.marginLeft = qBound(0.0, look.value(QStringLiteral("indent")).toDouble(), 400.0);
    }
    next.runs = {run};
    const QString masterId = master->id;
    change(tr("Text styles"), [&](ShowModel::Presentation &deck) {
        return ShowModel::setStyleLevel(deck, masterId, list, level, next);
    });
}

void ShowMasterView::renameCurrent(const QString &name)
{
    const QString layout = m_layoutId;
    const QString master = m_masterId;
    change(tr("Rename"), [&](ShowModel::Presentation &deck) {
        return layout.isEmpty() ? ShowModel::renameMaster(deck, master, name) : ShowModel::renameLayout(deck, layout, name);
    });
}

} // namespace QindaOffice::Apps::Show
