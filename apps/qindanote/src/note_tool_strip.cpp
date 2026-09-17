// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote's left tool strip: the four ink tools, the pen presentation
// controls, the page background and zoom pickers, undo/redo, and the three
// content-block commands. Split out of note_window.cpp to keep both inside
// the line budget.
//
// AGENT-GUARD: the strip shares the SHELL's Undo/Redo actions rather than
// cloning them — two actions answering to the same objectName in one
// window is exactly the identity collision AGENTS.md rule 5 forbids.

#include "note_window.h"

#include "note_find_bar.h"
#include "note_page_view.h"

#include "standard_actions.h"
#include "wrapping_tool_bar.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QSpinBox>
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
    const QList<std::tuple<QString, QLatin1String, NoteTool>> entries = {
        {tr("Pen"), QLatin1String("notePenTool"), NoteTool::Pen},
        {tr("Highlight"), QLatin1String("noteHighlighterTool"), NoteTool::Highlighter},
        {tr("Eraser"), QLatin1String("noteEraserTool"), NoteTool::Eraser},
        {tr("Text"), QLatin1String("noteTextTool"), NoteTool::Text},
    };
    QActionGroup *toolGroup = new QActionGroup(this);
    bool firstTool = true;
    for (const auto &[label, id, tool] : entries) {
        auto *action = new QAction(label, this);
        tools->addStripAction(action);
        // Stable objectName: the dbusmenu/global-menu activation identity
        // (AGENTS.md rule 5) — unnamed actions are unreachable from the panel.
        action->setObjectName(id);
        action->setCheckable(true);
        // Exclusive tool group: the earlier release let every tool appear
        // checked simultaneously and showed none as the initial state.
        toolGroup->addAction(action);
        action->setChecked(firstTool);
        firstTool = false;
        connect(action, &QAction::triggered, this, [this, action, tool] {
            m_view->setTool(tool);
            action->setChecked(true);
        });
    }
    tools->addStripSeparator();

    // M15 polish: pen width/color, page background and zoom live on the
    // tool strip (they are presentation state, not document commands, so
    // they are toolbar widgets rather than ActionId commands).
    auto *width = new QSpinBox(tools);
    width->setObjectName(QStringLiteral("penWidthSpin"));
    width->setRange(1, 24);
    width->setValue(int(m_view->penWidth()));
    width->setSuffix(tr(" px"));
    width->setToolTip(tr("Pen width"));
    tools->addStripWidget(width);
    connect(width, &QSpinBox::valueChanged, m_view,
            [this](int value) { m_view->setPenWidth(value); });

    auto *colorAction = new QAction(tr("Pen color"), this);
    tools->addStripAction(colorAction);
    colorAction->setObjectName(QStringLiteral("notePenColorAction"));
    connect(colorAction, &QAction::triggered, this, [this] {
        const QColor picked =
            QColorDialog::getColor(m_view->penColor(), this, tr("Pen Color"));
        if (picked.isValid()) {
            m_view->setPenColor(picked);
        }
    });

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
    // View > Zoom In/Out/Reset drive the same view state as the combo, and
    // the combo follows so the two never disagree. All three actions were
    // created disabled and never wired.
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
    connect(zoomActions->viewZoomIn, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(m_view->zoom() * 1.25);
        syncZoomCombo();
    });
    connect(zoomActions->viewZoomOut, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(m_view->zoom() / 1.25);
        syncZoomCombo();
    });
    connect(zoomActions->viewZoomReset, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(1.0);
        syncZoomCombo();
    });
    tools->addStripSeparator();
    // AGENT-GUARD: put the SHELL's Undo/Redo on the strip, do not clone them.
    // setDocumentHost() deleted the stub editor the base connections pointed
    // at, so Ctrl+Z fired into the void and the strip carried a second,
    // separate "Undo" action that was given the same objectName as the menu
    // one — two actions answering to `editUndoAction` in one window, which is
    // exactly the identity collision rule 5 forbids. One action per command:
    // the strip, the Edit menu and the exported global menu now share it.
    Shell::StandardActions *actions = standardActions();
    tools->addStripAction(actions->editUndo);
    tools->addStripAction(actions->editRedo);
    connect(actions->editUndo, &QAction::triggered, m_view, &NotePageView::undo);
    connect(actions->editRedo, &QAction::triggered, m_view, &NotePageView::redo);
    // Both commands follow the view's real history depth. Undo used to be
    // force-enabled with nothing to undo, and Redo was never wired at all.
    actions->editUndo->setEnabled(false);
    actions->editRedo->setEnabled(false);
    connect(m_view, &NotePageView::undoAvailable, actions->editUndo, &QAction::setEnabled);
    connect(m_view, &NotePageView::redoAvailable, actions->editRedo, &QAction::setEnabled);

    auto *text = new QAction(tr("Add text"), this);
    tools->addStripAction(text);
    text->setObjectName(QStringLiteral("noteAddTextAction"));
    connect(text, &QAction::triggered, m_view, &NotePageView::addTextBlock);

    auto *checklist = new QAction(tr("Add checklist item"), this);
    checklist->setObjectName(QStringLiteral("noteAddCheckAction"));
    checklist->setIconText(tr("Checklist"));
    tools->addStripAction(checklist);
    connect(checklist, &QAction::triggered, m_view, &NotePageView::addCheckItem);

    auto *image = new QAction(tr("Add image"), this);
    image->setObjectName(QStringLiteral("noteAddImageAction"));
    image->setIconText(tr("Image"));
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
    return tools;
}

} // namespace QindaOffice::Apps::Notes
