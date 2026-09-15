// SPDX-License-Identifier: GPL-3.0-or-later
// ShowWindow actions (plan section 6.3): starting the show (player on this
// window's screen, presenter on another), PDF export, and the AI seam
// methods the office_ai applier calls (append parsed outline slides,
// replace the current slide's text).

#include "show_window.h"

#include "pdf_export.h"
#include "presentation_controller.h"
#include "presenter_window.h"
#include "slide_player.h"

#include <QFileDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QStatusBar>
#include <QWindow>

namespace QindaOffice::Apps::Show {

void ShowWindow::startShow()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (deck->slides.isEmpty()) {
        return;
    }
    auto *controller = new PresentationController(deck, this);
    controller->goTo(qMax(0, m_currentRow));

    // The player owns the screen this window sits on; the presenter view
    // opens on another screen when one is available (probe #7: KWin
    // honours windowHandle()->setScreen).
    QScreen *here = windowHandle() != nullptr ? windowHandle()->screen() : nullptr;
    QScreen *other = nullptr;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen != here) {
            other = screen;
            break;
        }
    }
    auto *player = new SlidePlayer(controller, here, this);
    player->showFullScreen();
    if (other != nullptr) {
        auto *presenter = new PresenterWindow(controller, other, this);
        presenter->showFullScreen();
    }
}

void ShowWindow::exportPdf()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (deck->slides.isEmpty()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), QString(), tr("PDF files (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!exportDeckPdf(*deck, path, /*withNotesPages=*/true, &error)) {
        QMessageBox::warning(this, tr("Export PDF"), error);
    } else {
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    }
}

void ShowWindow::appendSlides(const QList<ShowModel::Slide> &slides)
{
    if (slides.isEmpty()) {
        return;
    }
    ShowModel::Presentation *deck = m_document->presentation();
    pushSlideListCommand(deck->slides);
    deck->slides.append(slides);
    m_currentRow = deck->slides.size() - 1;
    m_document->setDirty(true);
    fullRefresh();
}

void ShowWindow::replaceCurrentSlideText(const QString &text)
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
    m_document->setDirty(true);
    fullRefresh();
}

} // namespace QindaOffice::Apps::Show
