// SPDX-License-Identifier: GPL-3.0-or-later
// ShowWindow's interaction wiring: the canvas context menu, the "Slide n of
// m" status field, deck-wide Replace and the Preferences hook. Split out of
// show_window.cpp to keep both inside the line budget.

#include "show_window.h"

#include "show_arrange.h"
#include "show_commands.h"
#include "show_document.h"
#include "slide_canvas.h"

#include "preferences.h"

#include <QInputDialog>
#include <QMenu>
#include <QStatusBar>
#include <QUndoStack>

namespace QindaOffice::Apps::Show {

namespace {

// Replaces `needle` with `replacement` everywhere in one slide; returns how
// many hits it made so the status line can report honestly.
int replaceInSlide(ShowModel::Slide &slide, const QString &needle, const QString &replacement,
                   Qt::CaseSensitivity sensitivity)
{
    int hits = 0;
    const auto replaceIn = [&](QString &text) {
        const int before = text.count(needle, sensitivity);
        if (before > 0) {
            text.replace(needle, replacement, sensitivity);
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

void ShowWindow::wireCanvasInteraction()
{
    m_canvas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_canvas, &SlideCanvas::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("showCanvasContextMenu"));
        m_arrange->populateContextMenu(&menu);
        menu.exec(m_canvas->mapToGlobal(pos));
    });
    connect(m_canvas, &SlideCanvas::selectionChanged, this,
            [this](bool) { updateSlideStatus(); });

    // Replace was created disabled by createStandardActions() and never
    // wired, so QindaShow shipped a dead Edit menu entry (D-012).
    m_actions.editReplace->setEnabled(true);
    connect(m_actions.editReplace, &QAction::triggered, this,
            &ShowWindow::replaceAcrossDeck);
    updateSlideStatus();
}

void ShowWindow::updateSlideStatus()
{
    const int count = m_document->presentation()->slides.size();
    QString info = m_currentRow >= 0
        ? tr("Slide %1 of %2").arg(m_currentRow + 1).arg(count)
        : tr("No slides");
    if (m_canvas->slide() != nullptr) {
        const int shapes = m_canvas->slide()->shapes.size();
        const int selected = m_canvas->selectedShapeIds().size();
        info += tr("   Shapes: %1").arg(shapes);
        if (selected > 0) {
            info += tr(" (%1 selected)").arg(selected);
        }
    }
    info += tr("   Zoom: %1%").arg(qRound(m_canvas->zoom() * 100));
    setStatusInfo(info);
}

void ShowWindow::replaceAcrossDeck()
{
    bool ok = false;
    const QString needle =
        QInputDialog::getText(this, tr("Replace"), tr("Find what:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok || needle.isEmpty()) {
        return;
    }
    const QString replacement =
        QInputDialog::getText(this, tr("Replace"), tr("Replace with:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok) {
        return;
    }
    ShowModel::Presentation *deck = m_document->presentation();
    const QList<ShowModel::Slide> before = deck->slides;
    int hits = 0;
    for (ShowModel::Slide &slide : deck->slides) {
        hits += replaceInSlide(slide, needle, replacement, Qt::CaseSensitive);
    }
    if (hits == 0) {
        statusBar()->showMessage(tr("“%1” not found").arg(needle), 5000);
        deck->slides = before;
        return;
    }
    // A whole sweep is ONE undo step however many slides it touched.
    auto *command = new SlideListCommand(deck, before, deck->slides);
    command->setText(tr("Replace all"));
    m_document->undoStack()->push(command);
    statusBar()->showMessage(tr("Replaced %1 occurrence(s)").arg(hits), 5000);
}

void ShowWindow::applyPreferences()
{
    m_canvas->setZoom(Shell::Preferences::instance().defaultZoomPercent() / 100.0);
    updateSlideStatus();
}

} // namespace QindaOffice::Apps::Show
