// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's deck operations: new/add/duplicate/delete/move slides, the
// AI seams (append parsed outline slides, replace the current slide's
// text), find and replace across the deck, the show itself, templates and
// the PDF export. Split from show_host.cpp for the line budget.

#include "show_host.h"

#include "pdf_export.h"
#include "presentation_controller.h"
#include "show_commands.h"
#include "show_design_actions.h"

#include <QUndoStack>
#include <QUrl>
#include <QUuid>

namespace QindaOffice::Apps::Show {

namespace {

// Build a fresh slide for one of the six layouts (plan section 6.3).
ShowModel::Slide makeSlide(const QString &title, const QString &layout, const QSizeF &size)
{
    ShowModel::Slide slide;
    slide.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    slide.title = title;
    slide.layout = layout;
    auto textBox = [&](const QRectF &rect, const QString &text, int pointSize, bool bold) {
        ShowModel::Shape shape;
        shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shape.kind = ShowModel::ShapeKind::TextBox;
        shape.rect = rect;
        shape.strokeWidth = 0;
        ShowModel::TextRun run;
        run.text = text;
        run.pointSize = pointSize;
        run.bold = bold;
        shape.paragraphs = {{ShowModel::TextParagraph{.runs = {run}}}};
        slide.shapes.append(shape);
    };
    const QRectF titleRect(size.width() * 0.1, size.height() * 0.08, size.width() * 0.8, size.height() * 0.18);
    const QRectF body(size.width() * 0.1, size.height() * 0.35, size.width() * 0.8, size.height() * 0.5);
    if (layout == QLatin1String("title")) {
        textBox(titleRect, title, 44, true);
    } else if (layout == QLatin1String("title+content")) {
        textBox(titleRect, title, 36, true);
        textBox(body, QString(), 24, false);
    } else if (layout == QLatin1String("title+2content") || layout == QLatin1String("two-content")) {
        textBox(titleRect, title, 36, true);
        textBox(QRectF(size.width() * 0.06, size.height() * 0.35, size.width() * 0.42, size.height() * 0.5),
                QString(), 24, false);
        textBox(QRectF(size.width() * 0.52, size.height() * 0.35, size.width() * 0.42, size.height() * 0.5),
                QString(), 24, false);
    } else if (layout == QLatin1String("content")) {
        textBox(QRectF(size.width() * 0.1, size.height() * 0.15, size.width() * 0.8, size.height() * 0.7),
                QString(), 24, false);
    }
    return slide; // "blank" gets no shapes
}

// All searchable text of one slide: title, every run of every shape, notes.
QString slideText(const ShowModel::Slide &slide)
{
    QString text = slide.title;
    for (const ShowModel::Shape &shape : slide.shapes) {
        for (const ShowModel::TextParagraph &paragraph : shape.paragraphs) {
            for (const ShowModel::TextRun &run : paragraph.runs) {
                text += QLatin1Char('\n') + run.text;
            }
        }
    }
    return text + QLatin1Char('\n') + slide.notes;
}

// Replaces `needle` everywhere in one slide; returns the number of hits.
int replaceInSlide(ShowModel::Slide &slide, const QString &needle, const QString &replacement)
{
    int hits = 0;
    const auto replaceIn = [&](QString &text) {
        const int before = int(text.count(needle, Qt::CaseSensitive));
        if (before > 0) {
            text.replace(needle, replacement, Qt::CaseSensitive);
            hits += before;
        }
    };
    replaceIn(slide.title);
    replaceIn(slide.notes);
    for (ShowModel::Shape &shape : slide.shapes) {
        for (ShowModel::TextParagraph &paragraph : shape.paragraphs) {
            for (ShowModel::TextRun &run : paragraph.runs) {
                replaceIn(run.text);
            }
        }
    }
    return hits;
}

} // namespace

// AGENT-GUARD (every caller): mutate first, then push. QUndoStack::push
// runs redo() synchronously → indexChanged → fullRefresh(); the refresh
// must observe the NEW state and the command's after-image must be it.
void ShowHost::pushSlideListCommand(const QList<ShowModel::Slide> &before)
{
    ShowModel::Presentation *deck = m_document->presentation();
    m_document->undoStack()->push(new SlideListCommand(deck, before, deck->slides));
}

void ShowHost::newDeck()
{
    ShowModel::Presentation *deck = m_document->presentation();
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.clear();
    // A new deck opens with the layout's placeholder text boxes on it.
    deck->slides.append(makeSlide(tr("Slide 1"), QStringLiteral("title"), deck->slideSize));
    m_currentRow = 0;
    pushSlideListCommand(before);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::addSlide()
{
    ShowModel::Presentation *deck = m_document->presentation();
    const QStringList layouts = layoutNames();
    const QString layout = m_currentRow >= 0 ? layouts.at(layoutIndex()) : QStringLiteral("title+content");
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.append(makeSlide(tr("Slide %1").arg(deck->slides.size() + 1), layout, deck->slideSize));
    m_currentRow = int(deck->slides.size()) - 1;
    pushSlideListCommand(before);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::duplicateSlide()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size()) {
        return;
    }
    const QList<ShowModel::Slide> before = deck->slides;
    ShowModel::Slide copy = deck->slides.at(m_currentRow);
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (ShowModel::Shape &shape : copy.shapes) {
        shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    deck->slides.insert(m_currentRow + 1, copy);
    m_currentRow += 1;
    pushSlideListCommand(before);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::deleteSlide()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size()) {
        return;
    }
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.removeAt(m_currentRow);
    if (m_currentRow >= deck->slides.size()) {
        m_currentRow = int(deck->slides.size()) - 1;
    }
    pushSlideListCommand(before);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::moveSlide(int from, int to)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (from < 0 || to < 0 || from >= deck->slides.size() || to >= deck->slides.size() || from == to) {
        return;
    }
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.move(from, to);
    m_currentRow = to;
    auto *command = new SlideListCommand(deck, before, deck->slides);
    command->setText(tr("Move slide"));
    m_document->undoStack()->push(command);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::appendSlides(const QList<ShowModel::Slide> &slides)
{
    if (slides.isEmpty()) {
        return;
    }
    ShowModel::Presentation *deck = m_document->presentation();
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.append(slides);
    m_currentRow = int(deck->slides.size()) - 1;
    pushSlideListCommand(before);
    m_document->setDirty(true);
    fullRefresh();
}

void ShowHost::replaceCurrentSlideText(const QString &text)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size()) {
        return;
    }
    ShowModel::Slide &slide = deck->slides[m_currentRow];
    ShowModel::Shape *target = nullptr;
    for (ShowModel::Shape &shape : slide.shapes) {
        if (shape.kind == ShowModel::ShapeKind::TextBox) {
            target = &shape;
            break;
        }
    }
    const ShowModel::Slide before = slide;
    if (target == nullptr) {
        slide.title = text;
    } else {
        ShowModel::TextRun run;
        run.text = text;
        run.pointSize = 24;
        ShowModel::TextParagraph paragraph;
        paragraph.runs = {run};
        target->paragraphs = {paragraph};
    }
    if (before != slide) {
        m_document->undoStack()->push(new SlideStateCommand(deck, slide.id, before, slide));
    }
    m_document->setDirty(true);
    fullRefresh();
}

// ---- find and replace -------------------------------------------------------------

QList<int> ShowHost::matches(const QString &needle, bool caseSensitive) const
{
    QList<int> hits;
    if (needle.isEmpty()) {
        return hits;
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const ShowModel::Presentation *deck = m_document->presentation();
    for (int i = 0; i < deck->slides.size(); ++i) {
        if (slideText(deck->slides.at(i)).contains(needle, cs)) {
            hits.append(i);
        }
    }
    return hits;
}

bool ShowHost::findNext(const QString &needle, bool caseSensitive)
{
    const QList<int> hits = matches(needle, caseSensitive);
    if (hits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return false;
    }
    int next = hits.first();
    for (const int hit : hits) {
        if (hit > m_currentRow) {
            next = hit;
            break;
        }
    }
    selectSlideRow(next);
    showStatusMessage(tr("Slide %1 of %2 matching").arg(next + 1).arg(hits.size()));
    return true;
}

bool ShowHost::findPrevious(const QString &needle, bool caseSensitive)
{
    const QList<int> hits = matches(needle, caseSensitive);
    if (hits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return false;
    }
    int next = hits.last();
    for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
        if (*it < m_currentRow) {
            next = *it;
            break;
        }
    }
    selectSlideRow(next);
    showStatusMessage(tr("Slide %1 of %2 matching").arg(next + 1).arg(hits.size()));
    return true;
}

int ShowHost::replaceAll(const QString &needle, const QString &replacement)
{
    if (needle.isEmpty()) {
        return 0;
    }
    ShowModel::Presentation *deck = m_document->presentation();
    const QList<ShowModel::Slide> before = deck->slides;
    int hits = 0;
    for (ShowModel::Slide &slide : deck->slides) {
        hits += replaceInSlide(slide, needle, replacement);
    }
    if (hits == 0) {
        deck->slides = before;
        showStatusMessage(tr("“%1” not found").arg(needle));
        return 0;
    }
    // A whole sweep is ONE undo step however many slides it touched.
    auto *command = new SlideListCommand(deck, before, deck->slides);
    command->setText(tr("Replace all"));
    m_document->undoStack()->push(command);
    m_document->setDirty(true);
    showStatusMessage(tr("Replaced %1 occurrence(s)").arg(hits));
    return hits;
}

// ---- the show, dialogs, templates ------------------------------------------------

void ShowHost::startShow()
{
    if (slideCount() == 0 || m_player != nullptr) {
        return;
    }
    m_canvas->commitTextEdit(false);
    m_player = new PresentationController(m_document->presentation(), this);
    m_player->goTo(qMax(0, m_currentRow));
    emit playerChanged();
}

void ShowHost::endShow()
{
    if (m_player == nullptr) {
        return;
    }
    // The editor lands on the slide the show ended on.
    const int row = m_player->slide();
    m_player->deleteLater();
    m_player = nullptr;
    emit playerChanged();
    selectSlideRow(row);
}

void ShowHost::insertImageFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.isEmpty() && m_canvas->addImage(path, QRectF()).isEmpty()) {
        showStatusMessage(tr("Cannot read %1 as an image").arg(path), 8000);
    }
}

void ShowHost::applyFillColor(const QColor &color)
{
    if (color.isValid()) {
        m_canvas->setSelectedFill(color);
    }
}

void ShowHost::applyStrokeColor(const QColor &color)
{
    if (color.isValid()) {
        m_canvas->setSelectedStroke(color);
    }
}

QVariantList ShowHost::masters() const
{
    return m_design->mastersAsVariant();
}

QStringList ShowHost::masterNames() const
{
    return m_design->masterNames();
}

void ShowHost::applyMasters(const QVariantList &masters)
{
    m_design->applyMasters(masters);
}

void ShowHost::applyMasterToSlide(int masterIndex)
{
    m_design->applyMasterToSlide(masterIndex);
}

void ShowHost::exportPdfTo(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty() || slideCount() == 0) {
        return;
    }
    QString error;
    if (exportDeckPdf(*m_document->presentation(), path, /*withNotesPages=*/true, &error)) {
        showStatusMessage(tr("Exported %1").arg(path), 5000);
    } else {
        emit messageRequested(tr("Export PDF"), error, QStringLiteral("danger"));
    }
}

void ShowHost::loadTemplate(const QString &path)
{
    if (m_document->isDirty()) {
        m_pendingTemplate = path;
        emit templateConsentRequested(path);
        return;
    }
    loadTemplateResolved(path, QStringLiteral("discard"));
}

void ShowHost::loadTemplateResolved(const QString &path, const QString &choice)
{
    m_pendingTemplate.clear();
    if (choice == QLatin1String("cancel")) {
        return;
    }
    if (choice == QLatin1String("save")) {
        // A cancelled or failed save also cancels the template load.
        m_pendingTemplate = path;
        auto *once = new QObject(this);
        connect(this, &Quick::DocumentHost::saveFinished, once, [this, once, path](bool ok) {
            once->deleteLater();
            if (ok && m_pendingTemplate == path) {
                loadTemplateResolved(path, QStringLiteral("discard"));
            } else {
                m_pendingTemplate.clear();
            }
        });
        save();
        return;
    }
    QString error;
    if (!m_document->load(path, &error)) {
        showStatusMessage(tr("Template failed: %1").arg(error), 8000);
        return;
    }
    // Detach from the shared template path: the window hosts the content
    // as a dirty untitled document.
    m_document->adoptPath(QString());
    m_document->setDirty(true);
    documentReplaced();
}

} // namespace QindaOffice::Apps::Show
