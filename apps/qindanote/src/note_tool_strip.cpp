// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote's TOP tool strip (M22): the four ink tools and the two picker
// popup buttons come from NoteInkActions (the SAME actions the Ink menu
// exports); then the content-block commands; the background and zoom
// combos sit at the end until M25 replaces them with menu buttons.
//
// AGENT-GUARD: Undo/Redo do NOT appear here — they live on the shell's
// main toolbar and Edit menu as one action each; the strip used to carry
// duplicates answering to the same objectName, the identity collision
// AGENTS.md rule 5 forbids.

#include "note_window.h"

#include "note_find_bar.h"
#include "note_ink_actions.h"
#include "note_page_view.h"

#include "standard_actions.h"
#include "wrapping_tool_bar.h"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QStandardPaths>
#include <QStatusBar>

namespace QindaOffice::Apps::Notes {

Shell::WrappingToolBar *NoteWindow::buildToolStrip()
{
    auto *tools = new Shell::WrappingToolBar(tr("Note tools"), this);
    // AGENT-GUARD: a QToolBar without an objectName is skipped (with a
    // runtime warning) by QMainWindow::saveState/restoreState, and the
    // platform theme keys toolbars by it — same contract as the shell's
    // "mainToolBar".
    tools->setObjectName(QStringLiteral("noteToolsToolBar"));

    // Tools + colour/width popup buttons (the Ink menu's actions).
    m_inkActions->addToStrip(tools);
    tools->addStripSeparator();

    auto *text = new QAction(tr("Add text"), this);
    text->setObjectName(QStringLiteral("noteAddTextAction"));
    text->setIcon(QIcon::fromTheme(QStringLiteral("insert-text")));
    tools->addStripAction(text);
    connect(text, &QAction::triggered, m_view, &NotePageView::addTextBlock);

    auto *checklist = new QAction(tr("Add checklist item"), this);
    checklist->setObjectName(QStringLiteral("noteAddCheckAction"));
    checklist->setIconText(tr("Checklist"));
    checklist->setIcon(QIcon::fromTheme(QStringLiteral("checkbox")));
    tools->addStripAction(checklist);
    connect(checklist, &QAction::triggered, m_view, &NotePageView::addCheckItem);

    auto *image = new QAction(tr("Add image"), this);
    image->setObjectName(QStringLiteral("noteAddImageAction"));
    image->setIconText(tr("Image"));
    image->setIcon(QIcon::fromTheme(QStringLiteral("insert-image")));
    tools->addStripAction(image);
    connect(image, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Insert Image"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("Could not read %1").arg(path), 8000);
            return;
        }
        // The file's own bytes, verbatim: a notebook page is one
        // self-contained JSON file, so a path reference would break the
        // moment the notebook moved.
        m_view->addImageBlock(file.readAll());
        m_document->setDirty(true);
    });
    tools->addStripSeparator();

    // M25 replaces these two combos with [Bg v] / [100% v] menu buttons
    // over the Page ▸ Background and View ▸ Zoom actions; until then they
    // stay, moved to the END of the strip (plan section 3.2).
    auto *background = new QComboBox(tools);
    background->setObjectName(QStringLiteral("pageBackgroundCombo"));
    background->addItem(tr("Blank"), QStringLiteral("blank"));
    background->addItem(tr("Ruled"), QStringLiteral("ruled"));
    background->addItem(tr("Grid"), QStringLiteral("grid"));
    background->addItem(tr("Dotted"), QStringLiteral("dotted"));
    tools->addStripWidget(background);
    connect(background, &QComboBox::activated, this, [this, background](int index) {
        m_view->setBackground(background->itemData(index).toString());
    });

    auto *zoom = new QComboBox(tools);
    zoom->setObjectName(QStringLiteral("zoomCombo"));
    for (const int percent : {50, 75, 100, 150, 200}) {
        zoom->addItem(tr("%1%").arg(percent), percent);
    }
    zoom->setCurrentIndex(2);
    tools->addStripWidget(zoom);
    connect(zoom, &QComboBox::activated, this,
            [this, zoom](int index) { m_view->setZoom(zoom->itemData(index).toInt() / 100.0); });
    // View > Zoom In/Out/Reset, Ctrl+wheel and fit-width drive the same
    // view state as the combo, and the combo follows so the two never
    // disagree.
    const auto syncZoomCombo = [this, zoom] {
        const int percent = qRound(m_view->zoom() * 100.0);
        const int index = zoom->findData(percent);
        QSignalBlocker blocker(zoom);
        if (index >= 0) {
            zoom->setCurrentIndex(index);
        } else {
            zoom->setCurrentText(tr("%1%").arg(percent));
        }
    };
    connect(m_view, &NotePageView::presentationChanged, this, syncZoomCombo);
    // Find was three dead menu entries until the notebook gained a find bar.
    Shell::StandardActions *findActions = standardActions();
    for (QAction *action : {findActions->editFind, findActions->editFindNext,
                            findActions->editFindPrevious}) {
        action->setEnabled(true);
    }
    connect(findActions->editFind, &QAction::triggered, m_findBar, &NoteFindBar::openFind);
    connect(findActions->editFindNext, &QAction::triggered, m_findBar,
            &NoteFindBar::findNext);
    connect(findActions->editFindPrevious, &QAction::triggered, m_findBar,
            &NoteFindBar::findPrevious);
    connect(m_findBar, &NoteFindBar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    connect(m_findBar, &NoteFindBar::pageRequested, this,
            [this](const QString &sectionId, const QString &pageId) {
                selectPage(sectionId, pageId);
            });

    // Print and preview: created disabled and never wired.
    Shell::StandardActions *printActions = standardActions();
    printActions->filePrint->setEnabled(true);
    connect(printActions->filePrint, &QAction::triggered, this,
            [this] { Shell::PrintHelper::print(this, printRenderCallback()); });
    printActions->filePrintPreview->setEnabled(true);
    connect(printActions->filePrintPreview, &QAction::triggered, this,
            [this] { Shell::PrintHelper::printPreview(this, printRenderCallback()); });

    Shell::StandardActions *zoomActions = standardActions();
    for (QAction *action : {zoomActions->viewZoomIn, zoomActions->viewZoomOut,
                            zoomActions->viewZoomReset}) {
        action->setEnabled(true);
    }
    connect(zoomActions->viewZoomIn, &QAction::triggered, this, [this] {
        m_view->setZoom(m_view->zoom() * 1.25);
    });
    connect(zoomActions->viewZoomOut, &QAction::triggered, this, [this] {
        m_view->setZoom(m_view->zoom() / 1.25);
    });
    connect(zoomActions->viewZoomReset, &QAction::triggered, this,
            [this] { m_view->setZoom(1.0); });

    // The shell's Undo/Redo drive the view's ONE history (the strip itself
    // stays free of them — the main toolbar already shows them).
    Shell::StandardActions *actions = standardActions();
    connect(actions->editUndo, &QAction::triggered, m_view, &NotePageView::undo);
    connect(actions->editRedo, &QAction::triggered, m_view, &NotePageView::redo);
    // Both commands follow the view's real history depth. Undo used to be
    // force-enabled with nothing to undo, and Redo was never wired at all.
    actions->editUndo->setEnabled(false);
    actions->editRedo->setEnabled(false);
    connect(m_view, &NotePageView::undoAvailable, actions->editUndo, &QAction::setEnabled);
    connect(m_view, &NotePageView::redoAvailable, actions->editRedo, &QAction::setEnabled);
    return tools;
}

} // namespace QindaOffice::Apps::Notes
