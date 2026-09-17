// SPDX-License-Identifier: GPL-3.0-or-later
// ShowWindow actions (plan section 6.3): starting the show (player on this
// window's screen, presenter on another), PDF export, and the AI seam
// methods the office_ai applier calls (append parsed outline slides,
// replace the current slide's text).

#include "show_window.h"

#include "show_arrange.h"
#include "show_design_actions.h"

#include "pdf_export.h"
#include "show_clipboard.h"
#include "show_commands.h"
#include "show_toolbox.h"
#include "template_loader.h"
#include "presentation_controller.h"
#include "presenter_window.h"
#include "slide_player.h"

#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>

#include "inline_action_flow.h"
#include "show_ai.h"
#include "show_find_bar.h"
#include "slide_canvas.h"
#include "slide_renderer.h"
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QScreen>
#include <QStatusBar>
#include <QWindow>

namespace QindaOffice::Apps::Show {

namespace {
// Turns a display name into a stable, valid objectName suffix.
QString sanitizedId(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}
} // namespace

void ShowWindow::openTemplateAsUntitled(const QString &path)
{
    QString error;
    if (m_document->load(path, &error)) {
        // Detach from the shared template path: the window hosts the
        // content as a dirty untitled document.
        m_document->adoptPath(QString());
        m_document->setDirty(true);
        fullRefresh();
    } else {
        statusBar()->showMessage(tr("Template failed: %1").arg(error), 8000);
    }
}

void ShowWindow::insertFileMenuActions(QMenu *fileMenu)
{
    const QList<Shell::TemplateEntry> templates = Shell::templatesFor(QStringLiteral("QindaShow"));
    if (templates.isEmpty()) {
        return;
    }
    // AGENT-GUARD: insert before the shell's File-menu anchor. addMenu()
    // appends, which landed "New from Template" BELOW Quit.
    auto *submenu = new QMenu(tr("New from &Template"), this);
    submenu->setObjectName(QStringLiteral("fileTemplateMenu"));
    fileMenu->insertMenu(fileMenuAnchor(), submenu);
    for (const Shell::TemplateEntry &entry : templates) {
        QAction *item = submenu->addAction(entry.name);
        // AGENT-GUARD: stable objectName or the desktop global menu cannot
        // address the command at all (AGENTS.md rule 5).
        item->setObjectName(QStringLiteral("showTemplate_%1").arg(sanitizedId(entry.name)));
        connect(item, &QAction::triggered, this, [this, path = entry.path] {
            // A template replaces the deck: consent first when dirty.
            if (!confirmDiscardChanges()) {
                return;
            }
            openTemplateAsUntitled(path);
        });
    }
}

bool ShowWindow::confirmDiscardChanges()
{
    if (!m_document->isDirty()) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("Loading the template discards unsaved changes to the current "
           "presentation. Continue?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    return answer != QMessageBox::Save || saveDocument();
}

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
        // AGENT-GUARD: the presenter must not outlive the show. Without this
        // coupling, closing the player left a fullscreen presenter stranded
        // on the second screen, ignoring Escape and clicks.
        connect(player, &QObject::destroyed, presenter, &QObject::deleteLater);
    }
}

Shell::PrintRenderCallback ShowWindow::printRenderCallback() const
{
    const ShowModel::Presentation *deck = m_document->presentation();
    // The deck outlives the synchronous print call (PrintHelper runs the
    // dialog and the render before returning), so capturing it is safe.
    return [deck](QPrinter *printer, QPainter *painter) {
        if (deck->slides.isEmpty()) {
            return;
        }
        const QSizeF slide = deck->slideSize;
        const QRectF page(0, 0, printer->width(), printer->height());
        for (int i = 0; i < deck->slides.size(); ++i) {
            if (i > 0) {
                printer->newPage();
            }
            // Letterbox the slide into whatever paper the user chose rather
            // than forcing the deck's own page size on their printer.
            const qreal scale = qMin(page.width() / slide.width(),
                                     page.height() / slide.height());
            painter->save();
            painter->translate(page.center()
                               - QPointF(slide.width() * scale / 2.0,
                                         slide.height() * scale / 2.0));
            painter->scale(scale, scale);
            ShowModel::renderSlide(painter, deck->slides.at(i), slide);
            painter->restore();
        }
    };
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
    // Mutate first, then push (see pushSlideListCommand callers in
    // show_window.cpp — push runs redo()/fullRefresh() synchronously).
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.append(slides);
    pushSlideListCommand(before);
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
    // Snapshot for undo (SH: the AI rewrite must be undoable like every
    // other mutation; it previously bypassed the stack entirely).
    ShowModel::Slide before = slide;
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
    if (m_document->undoStack() != nullptr && before != slide) {
        m_document->undoStack()->push(new SlideStateCommand(deck, slide.id, before, slide));
    }
    m_document->setDirty(true);
    fullRefresh();
}

void ShowWindow::insertApplicationMenus(QMenu *before)
{
    QMenu *slideMenu = new QMenu(tr("Slide"), this);
    slideMenu->addAction(m_newSlideAction);
    slideMenu->addAction(m_duplicateSlideAction);
    slideMenu->addAction(m_deleteSlideAction);
    slideMenu->addSeparator();
    m_arrange->populateSlideMenu(slideMenu);
    slideMenu->addSeparator();
    m_design->populateSlideMenu(slideMenu);
    if (auto *viewMenu = findChild<QMenu *>(QStringLiteral("viewMenu"))) {
        viewMenu->addSeparator();
        m_design->populateViewMenu(viewMenu);
    }
    slideMenu->addSeparator();
    slideMenu->addAction(m_startShowAction);
    slideMenu->addSeparator();
    QAction *exportPdfAction = slideMenu->addAction(tr("Export PDF..."));
    exportPdfAction->setObjectName(QStringLiteral("showExportPdf"));
    connect(exportPdfAction, &QAction::triggered, this, [this] { exportPdf(); });
    slideMenu->setObjectName(QStringLiteral("showSlideMenu"));
    menuBar()->insertMenu(before->menuAction(), slideMenu);

    // AI actions go into the shell's AI menu next to the panel toggle.
    before->addSeparator();
    for (const Ai::InlineAction &action : Ai::inlineActions()) {
        QAction *item = before->addAction(action.displayName);
        item->setObjectName(QStringLiteral("showAi%1").arg(action.id));
        connect(item, &QAction::triggered, this, [this, id = action.id] {
            m_inline->run(id, this);
        });
    }
    before->addSeparator();
    QAction *generate = before->addAction(tr("Generate deck from outline…"));
    generate->setObjectName(QStringLiteral("showGenerateDeck"));
    connect(generate, &QAction::triggered, this,
            [this] { generateDeckFromOutline(this, m_catalog, this); });

    // The insert commands are persistent toolbox actions shared with the
    // shapes toolbar (action discipline, AGENTS.md rule 5).
    QMenu *insertMenu = new QMenu(tr("Insert"), this);
    insertMenu->setObjectName(QStringLiteral("showInsertMenu"));
    for (QAction *action : m_toolbox->insertActions().all()) {
        insertMenu->addAction(action);
    }
    insertMenu->addSeparator();
    QAction *appearOnClick = insertMenu->addAction(tr("Appear on click"));
    appearOnClick->setObjectName(QStringLiteral("showAppearOnClick"));
    connect(appearOnClick, &QAction::triggered, this,
            [this] { m_canvas->toggleBuildSelected(); });
    menuBar()->insertMenu(before->menuAction(), insertMenu);

    QMenu *shapeMenu = new QMenu(tr("S&hape"), this);
    shapeMenu->setObjectName(QStringLiteral("showShapeMenu"));
    menuBar()->insertMenu(before->menuAction(), shapeMenu);
    for (QAction *action : m_toolbox->styleActions().styleActions()) {
        shapeMenu->addAction(action);
    }
    m_design->populateShapeMenu(shapeMenu);

    QMenu *arrangeMenu = new QMenu(tr("Arrange"), this);
    arrangeMenu->setObjectName(QStringLiteral("showArrangeMenu"));
    m_arrange->populateArrangeMenu(arrangeMenu);
    menuBar()->insertMenu(before->menuAction(), arrangeMenu);
}

void ShowWindow::wireCanvasUndo()
{
    // A gesture that touches several shapes brackets itself; everything
    // inside becomes one command instead of one per shape.
    connect(m_canvas, &SlideCanvas::compoundEditStarted, this, [this] {
        if (m_canvas->slide() != nullptr) {
            m_compoundBefore = *m_canvas->slide();
            m_compoundEdit = true;
        }
    });
    connect(m_canvas, &SlideCanvas::compoundEditFinished, this, [this] {
        if (!m_compoundEdit || m_canvas->slide() == nullptr) {
            m_compoundEdit = false;
            return;
        }
        m_compoundEdit = false;
        ShowModel::Slide *slide = m_canvas->slide();
        if (*slide == m_compoundBefore) {
            return;
        }
        m_document->undoStack()->push(
            new SlideStateCommand(m_document->presentation(), slide->id, m_compoundBefore,
                                  *slide));
    });
    connect(m_canvas, &SlideCanvas::shapeInserted, this, [this](const QString &shapeId) {
        ShowModel::Shape *shape = m_canvas->slide()->findShape(shapeId);
        if (shape != nullptr) {
            m_document->undoStack()->push(
                new InsertShapeCommand(m_canvas->slide(), shapeId));
        }
    });
    connect(m_canvas, &SlideCanvas::shapeRemoved, this,
            [this](int index, const ShowModel::Shape &removed) {
                m_document->undoStack()->push(
                    new RemoveShapeCommand(m_canvas->slide(), index, removed));
            });
    connect(m_canvas, &SlideCanvas::shapeGeometryEdited, this,
            [this](const QString &shapeId, const QRectF &oldRect, const QRectF &newRect) {
                if (m_compoundEdit) {
                    return; // folded into the compound command
                }
                m_document->undoStack()->push(
                    new GeometryCommand(m_canvas->slide(), shapeId, oldRect, newRect));
            });
    connect(m_canvas, &SlideCanvas::shapeTextEdited, this,
            [this](const QString &shapeId, const QList<ShowModel::TextParagraph> &oldText,
                   const QList<ShowModel::TextParagraph> &newText) {
                m_document->undoStack()->push(
                    new TextEditCommand(m_canvas->slide(), shapeId, oldText, newText));
            });
    connect(m_canvas, &SlideCanvas::shapeBuildEdited, this,
            [this](const QString &shapeId, int oldStep, int newStep) {
                m_document->undoStack()->push(
                    new BuildStepCommand(m_canvas->slide(), shapeId, oldStep, newStep));
            });
    connect(m_canvas, &SlideCanvas::shapesReordered, this,
            [this](const QStringList &oldOrder, const QStringList &newOrder) {
                m_document->undoStack()->push(
                    new ShapeOrderCommand(m_canvas->slide(), oldOrder, newOrder));
            });
    // M13 styling ops arrive as whole-shape before/after states.
    connect(m_canvas, &SlideCanvas::shapeStyled, this,
            [this](const QString &shapeId, const ShowModel::Shape &before,
                   const ShowModel::Shape &after) {
                m_document->undoStack()->push(
                    new ShapeStateCommand(m_canvas->slide(), shapeId, before, after));
            });
}

void ShowWindow::wireShowEditingActions()
{
    // Print, preview and zoom were created disabled and never wired, so all
    // three sat greyed out in the menus forever.
    m_actions.filePrint->setEnabled(true);
    connect(m_actions.filePrint, &QAction::triggered, this,
            [this] { Shell::PrintHelper::print(this, printRenderCallback()); });
    m_actions.filePrintPreview->setEnabled(true);
    connect(m_actions.filePrintPreview, &QAction::triggered, this,
            [this] { Shell::PrintHelper::printPreview(this, printRenderCallback()); });

    for (QAction *action : {m_actions.viewZoomIn, m_actions.viewZoomOut,
                            m_actions.viewZoomReset}) {
        action->setEnabled(true);
    }
    connect(m_actions.viewZoomIn, &QAction::triggered, this,
            [this] { m_canvas->setZoom(m_canvas->zoom() * 1.25); });
    connect(m_actions.viewZoomOut, &QAction::triggered, this,
            [this] { m_canvas->setZoom(m_canvas->zoom() / 1.25); });
    connect(m_actions.viewZoomReset, &QAction::triggered, this,
            [this] { m_canvas->setZoom(1.0); });

    m_actions.editCopy->setEnabled(false);
    m_actions.editCut->setEnabled(false);
    connect(m_canvas, &SlideCanvas::selectionChanged, this, [this](bool hasSelection) {
        m_actions.editCopy->setEnabled(hasSelection);
        m_actions.editCut->setEnabled(hasSelection);
    });
    connect(m_actions.editCopy, &QAction::triggered, this,
            [this] { copySelectedShape(m_canvas); });
    connect(m_actions.editCut, &QAction::triggered, this,
            [this] { cutSelectedShape(m_canvas); });

    m_actions.editPaste->setEnabled(false);
    auto updatePaste = [this] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        m_actions.editPaste->setEnabled(mime != nullptr
                                        && mime->hasFormat(
                                            QString::fromLatin1(shapeClipboardMimeType())));
    };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
    connect(m_actions.editPaste, &QAction::triggered, this,
            [this] { pasteShapeAtClipboard(m_canvas); });

    // Find was three dead menu entries until the deck gained a find bar.
    for (QAction *action : {m_actions.editFind, m_actions.editFindNext,
                            m_actions.editFindPrevious}) {
        action->setEnabled(true);
    }
    connect(m_actions.editFind, &QAction::triggered, m_findBar, &ShowFindBar::openFind);
    connect(m_actions.editFindNext, &QAction::triggered, m_findBar, &ShowFindBar::findNext);
    connect(m_actions.editFindPrevious, &QAction::triggered, m_findBar,
            &ShowFindBar::findPrevious);

    m_actions.editSelectAll->setEnabled(true);
    connect(m_actions.editSelectAll, &QAction::triggered, m_canvas,
            &SlideCanvas::selectAllShapes);
    connect(m_toolbox, &ShowToolbox::imageInsertRequested, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Insert image"));
        if (!path.isEmpty()) {
            m_canvas->addImage(path, QRectF());
        }
    });
}

} // namespace QindaOffice::Apps::Show
