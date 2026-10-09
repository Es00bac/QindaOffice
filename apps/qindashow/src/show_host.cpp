// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_host.h"

#include "presentation_controller.h"
#include "preferences.h"
#include "show_arrange.h"
#include "show_clipboard.h"
#include "show_commands.h"
#include "show_design_actions.h"
#include "show_master_view.h"
#include "show_slide_layouts.h"
#include "show_toolbox.h"
#include "slide_renderer.h"
#include "standard_actions.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QUndoStack>

#include <iterator>
#include <optional>

#include "desktop_settings.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {
// The model's names (the pptx mapping is pptx_timing.cpp); M67 adds Wipe
// and Split, and "slide" is labelled Push — it is PowerPoint's Push.
const char *const kTransitions[] = {"cut", "fade", "slide", "zoom", "wipe", "split"};
constexpr int kTransitionCount = int(std::size(kTransitions));
const char *const kLayouts[] = {"title", "title+content", "title+2content", "content", "two-content", "blank"};
} // namespace

ShowHost::ShowHost()
    : m_document(new ShowDocument)
    , m_canvas(new SlideCanvasItem)
    , m_idleDocument(new QTextDocument(this))
{
    setDocument(m_document);
    m_canvas->setObjectName(QStringLiteral("slideCanvas"));
    // The controllers own their command sets; the window renders them.
    m_toolbox = new ShowToolbox(m_canvas, actions(), this);
    m_arrange = new ShowArrangeActions(this);
    m_design = new ShowDesignActions(this);
    connect(m_toolbox, &ShowToolbox::imageInsertRequested, this, &ShowHost::imageDialogRequested);
    connect(m_toolbox, &ShowToolbox::fillColorRequested, this, &ShowHost::fillColorRequested);
    connect(m_toolbox, &ShowToolbox::strokeColorRequested, this, &ShowHost::strokeColorRequested);
    connect(m_design, &ShowDesignActions::mastersRequested, this, &ShowHost::mastersRequested);
    connect(m_design, &ShowDesignActions::applyMasterRequested, this, &ShowHost::applyMasterRequested);
    connect(m_document->undoStack(), &QUndoStack::indexChanged, this, [this] { fullRefresh(); });
    connect(m_canvas, &SlideCanvasItem::slideEdited, this, [this] { markEdited(); });
    connect(m_canvas, &SlideCanvasItem::selectionChanged, this, [this] { updateSlideStatus(); });
    connect(m_canvas, &SlideCanvasItem::contextMenuRequested, this, &ShowHost::contextMenuRequested);
    connect(m_canvas, &SlideCanvasItem::editingChanged, this, [this] {
        // The window's TextEdit shows the shape's document while editing
        // and an idle one otherwise (a null document is not allowed).
        if (m_editor == nullptr) {
            return;
        }
        auto *quickDocument = m_editor->property("textDocument").value<QQuickTextDocument *>();
        if (quickDocument != nullptr) {
            QTextDocument *live = m_canvas->editorDocument();
            quickDocument->setTextDocument(live != nullptr ? live : m_idleDocument);
        }
    });
    createAssistant();
    createEditing(); // M67: before the menus, which name its commands
    createDesign();  // M75: likewise
    insertMenus();
    designMenus();
    wireCanvasUndo();
    wireEditCommands();
    finalizeHost();
    fullRefresh();
}

ShowHost::~ShowHost()
{
    // A command destroyed with the stack must not re-enter fullRefresh()
    // through the indexChanged lambda on a half-dead host.
    disconnect(m_document->undoStack(), nullptr, this, nullptr);
    delete m_canvas;
    m_canvas = nullptr;
}

void ShowHost::attachCanvas(QQuickItem *container)
{
    m_canvas->setParentItem(container);
    m_canvas->setPosition(QPointF(0, 0));
    applyPreferences();
}

void ShowHost::attachEditor(QObject *textEdit)
{
    m_editor = textEdit;
}

// ---- the current slide -------------------------------------------------------------

int ShowHost::slideCount() const
{
    return int(m_document->presentation()->slides.size());
}

void ShowHost::fullRefresh()
{
    ++m_revision;
    const int count = slideCount();
    if (m_currentRow < 0 && count > 0) {
        m_currentRow = 0;
    }
    if (m_currentRow >= count) {
        m_currentRow = count - 1;
    }
    if (masterViewActive()) {
        m_masterView->refreshCanvas(); // the canvas holds a master or layout (M75)
        emit currentSlideChanged();
    } else if (m_currentRow >= 0) {
        switchSlide(m_currentRow);
    } else {
        m_canvas->setSlide(nullptr, m_document->presentation()->slideSize);
    }
    emit deckChanged();
    updateSlideStatus();
}

void ShowHost::switchSlide(int row)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (row < 0 || row >= deck->slides.size()) {
        return;
    }
    m_currentRow = row;
    // The backdrop first (background, master and layout decorations, the
    // master's type defaults): setSlide repaints.
    const ShowModel::Slide &current = deck->slides.at(row);
    m_canvas->setBackdrop(ShowModel::backdropFor(*deck, current), deck->masterFor(current));
    m_canvas->setPlaceholderLooks(ShowSlideLayouts::emptyLooks(*deck, current)); // M82
    m_canvas->setSlide(&deck->slides[row], deck->slideSize);
    m_arrange->syncFromSelection();
    m_design->syncFromSelection();
    m_toolbox->syncFromSelection();
    emit currentSlideChanged();
    updateSlideStatus();
}

void ShowHost::selectSlideRow(int row)
{
    if (masterViewActive()) {
        // The canvas shows a master or layout: only the row moves (M75).
        if (row >= 0 && row < slideCount()) {
            m_currentRow = row;
            emit currentSlideChanged();
        }
        return;
    }
    if (row >= 0 && row < slideCount() && row != m_currentRow) {
        switchSlide(row);
    } else if (row == m_currentRow) {
        switchSlide(row);
    }
}

QString ShowHost::slideTitle(int row) const
{
    const ShowModel::Presentation *deck = m_document->presentation();
    return row >= 0 && row < deck->slides.size() ? deck->slides.at(row).title : QString();
}

QString ShowHost::notes() const
{
    const ShowModel::Presentation *deck = m_document->presentation();
    return m_currentRow >= 0 && m_currentRow < deck->slides.size() ? deck->slides.at(m_currentRow).notes
                                                                    : QString();
}

void ShowHost::setNotes(const QString &notes)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size() || deck->slides.at(m_currentRow).notes == notes) {
        return;
    }
    // M67: undoable; consecutive typing in one slide's notes is one step.
    const ShowModel::Slide &slide = deck->slides.at(m_currentRow);
    m_document->undoStack()->push(new NotesEditCommand(deck, slide.id, slide.notes, notes));
    markEdited();
}

int ShowHost::transitionIndex() const
{
    const QString transition = m_currentRow >= 0 && m_currentRow < slideCount()
        ? m_document->presentation()->slides.at(m_currentRow).transition
        : QString();
    for (int i = 0; i < kTransitionCount; ++i) {
        if (transition == QLatin1String(kTransitions[i])) {
            return i;
        }
    }
    return 0;
}

void ShowHost::setTransitionIndex(int index)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (index < 0 || index >= kTransitionCount || m_currentRow < 0 || m_currentRow >= deck->slides.size()
        || transitionIndex() == index) {
        return;
    }
    deck->slides[m_currentRow].transition = QLatin1String(kTransitions[index]);
    markEdited();
}

int ShowHost::layoutIndex() const
{
    const QString layout = m_currentRow >= 0 && m_currentRow < slideCount()
        ? m_document->presentation()->slides.at(m_currentRow).layout
        : QString();
    const int index = int(layoutNames().indexOf(layout));
    return index >= 0 ? index : 0;
}

void ShowHost::setLayoutIndex(int index)
{
    ShowModel::Presentation *deck = m_document->presentation();
    const QStringList names = layoutNames();
    if (index < 0 || index >= names.size() || m_currentRow < 0 || m_currentRow >= deck->slides.size()
        || deck->slides.at(m_currentRow).layout == names.at(index)) {
        return;
    }
    deck->slides[m_currentRow].layout = names.at(index);
    markEdited();
}

QStringList ShowHost::transitionNames() const
{
    return {tr("Cut"), tr("Fade"), tr("Push"), tr("Zoom"), tr("Wipe"), tr("Split")};
}

QStringList ShowHost::layoutNames() const
{
    QStringList names;
    for (const char *layout : kLayouts) {
        names.append(QString::fromLatin1(layout));
    }
    return names;
}

QStringList ShowHost::fontFamilies() const
{
    return QFontDatabase::families();
}

bool ShowHost::reducedMotion() const
{
    return Quick::DesktopSettings::reducedMotion();
}

void ShowHost::markEdited()
{
    m_document->setDirty(true);
    m_document->notifyContentChanged();
    ++m_revision;
    emit deckChanged();
    updateSlideStatus();
}

void ShowHost::updateSlideStatus()
{
    const int count = slideCount();
    QString info = m_currentRow >= 0 ? tr("Slide %1 of %2").arg(m_currentRow + 1).arg(count) : tr("No slides");
    if (m_canvas->slide() != nullptr) {
        info += tr("   Shapes: %1").arg(m_canvas->slide()->shapes.size());
        if (m_canvas->selectionCount() > 0) {
            info += tr(" (%1 selected)").arg(m_canvas->selectionCount());
        }
    }
    setStatusInfo(info);
}

// ---- undo wiring ---------------------------------------------------------------------

void ShowHost::wireCanvasUndo()
{
    // AGENT-GUARD (M75, show_master_view.h): in the Slide Master view the
    // canvas edits a master or layout that is not one of the deck's slides
    // — the view commits those edits itself, so no slide command is pushed.
    // Every command finds its slide by id (show_commands.h).
    const auto slideRef = [this]() -> std::optional<SlideRef> {
        if (masterViewActive() || m_canvas->slide() == nullptr) {
            return std::nullopt;
        }
        return SlideRef{m_document->presentation(), m_canvas->slide()->id};
    };
    // A gesture that touches several shapes brackets itself; everything
    // inside becomes one command instead of one per shape.
    connect(m_canvas, &SlideCanvasItem::compoundEditStarted, this, [this] {
        if (m_canvas->slide() != nullptr && !masterViewActive()) {
            m_compoundBefore = *m_canvas->slide();
            m_compoundEdit = true;
        }
    });
    connect(m_canvas, &SlideCanvasItem::compoundEditFinished, this, [this] {
        ShowModel::Slide *slide = m_canvas->slide();
        const bool active = m_compoundEdit;
        m_compoundEdit = false;
        if (!active || slide == nullptr || *slide == m_compoundBefore || masterViewActive()) {
            return;
        }
        m_document->undoStack()->push(
            new SlideStateCommand(m_document->presentation(), slide->id, m_compoundBefore, *slide));
    });
    connect(m_canvas, &SlideCanvasItem::shapeInserted, this, [this, slideRef](const QString &shapeId) {
        if (const auto ref = slideRef()) {
            m_document->undoStack()->push(new InsertShapeCommand(*ref, shapeId));
        }
    });
    connect(m_canvas, &SlideCanvasItem::shapeRemoved, this, [this, slideRef](int index, const ShowModel::Shape &removed) {
        if (const auto ref = slideRef()) {
            m_document->undoStack()->push(new RemoveShapeCommand(*ref, index, removed));
        }
    });
    connect(m_canvas, &SlideCanvasItem::shapeGeometryEdited, this,
            [this, slideRef](const QString &shapeId, const QRectF &oldRect, const QRectF &newRect) {
                if (const auto ref = slideRef(); ref && !m_compoundEdit) {
                    m_document->undoStack()->push(new GeometryCommand(*ref, shapeId, oldRect, newRect));
                }
            });
    connect(m_canvas, &SlideCanvasItem::shapeTextEdited, this,
            [this, slideRef](const QString &shapeId, const QList<ShowModel::TextParagraph> &oldText,
                             const QList<ShowModel::TextParagraph> &newText) {
                if (const auto ref = slideRef()) {
                    m_document->undoStack()->push(new TextEditCommand(*ref, shapeId, oldText, newText));
                }
            });
    connect(m_canvas, &SlideCanvasItem::shapeBuildEdited, this,
            [this, slideRef](const QString &shapeId, int oldStep, int newStep) {
                if (const auto ref = slideRef()) {
                    m_document->undoStack()->push(new BuildStepCommand(*ref, shapeId, oldStep, newStep));
                }
            });
    connect(m_canvas, &SlideCanvasItem::shapesReordered, this,
            [this, slideRef](const QStringList &oldOrder, const QStringList &newOrder) {
                if (const auto ref = slideRef()) {
                    m_document->undoStack()->push(new ShapeOrderCommand(*ref, oldOrder, newOrder));
                }
            });
    connect(m_canvas, &SlideCanvasItem::shapeStyled, this,
            [this, slideRef](const QString &shapeId, const ShowModel::Shape &before, const ShowModel::Shape &after) {
                if (const auto ref = slideRef()) {
                    m_document->undoStack()->push(new ShapeStateCommand(*ref, shapeId, before, after));
                }
            });
}

void ShowHost::wireEditCommands()
{
    ActionEntry *undo = action(QString::fromLatin1(ActionId::kEditUndo));
    ActionEntry *redo = action(QString::fromLatin1(ActionId::kEditRedo));
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(m_document->undoStack(), &QUndoStack::canUndoChanged, undo, &ActionEntry::setEnabled);
    connect(m_document->undoStack(), &QUndoStack::canRedoChanged, redo, &ActionEntry::setEnabled);
    connect(undo, &ActionEntry::triggered, this, [this] { m_document->undoStack()->undo(); });
    connect(redo, &ActionEntry::triggered, this, [this] { m_document->undoStack()->redo(); });

    ActionEntry *copy = action(QString::fromLatin1(ActionId::kEditCopy));
    ActionEntry *cut = action(QString::fromLatin1(ActionId::kEditCut));
    ActionEntry *paste = action(QString::fromLatin1(ActionId::kEditPaste));
    copy->setEnabled(false);
    cut->setEnabled(false);
    connect(m_canvas, &SlideCanvasItem::selectionChanged, this, [this, copy, cut] {
        copy->setEnabled(m_canvas->hasSelection());
        cut->setEnabled(m_canvas->hasSelection());
    });
    connect(copy, &ActionEntry::triggered, this, [this] { copySelectedShape(m_canvas); });
    connect(cut, &ActionEntry::triggered, this, [this] { cutSelectedShape(m_canvas); });
    const auto updatePaste = [paste] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        paste->setEnabled(mime != nullptr && mime->hasFormat(QString::fromLatin1(shapeClipboardMimeType())));
    };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
    connect(paste, &ActionEntry::triggered, this, [this] { pasteShapeAtClipboard(m_canvas); });

    for (const char *id : {ActionId::kEditFind, ActionId::kEditFindNext, ActionId::kEditFindPrevious,
                           ActionId::kEditReplace, ActionId::kEditSelectAll, ActionId::kViewZoomIn,
                           ActionId::kViewZoomOut, ActionId::kViewZoomReset}) {
        action(QString::fromLatin1(id))->setEnabled(true);
    }
    connect(action(QString::fromLatin1(ActionId::kEditFind)), &ActionEntry::triggered, this,
            [this] { emit findRequested(); });
    connect(action(QString::fromLatin1(ActionId::kEditReplace)), &ActionEntry::triggered, this,
            [this] { emit replaceRequested(); });
    connect(action(QString::fromLatin1(ActionId::kEditSelectAll)), &ActionEntry::triggered, m_canvas,
            &SlideCanvasItem::selectAllShapes);
}

// ---- hooks ---------------------------------------------------------------------------

// Mirrors ShowDocument::mimeTypes() and the .desktop MimeType list.
QString ShowHost::filterString() const
{
    return tr("Presentations (*.pptx *.ppt *.odp *.fodp *.pptm *.potx *.ppsx);;"
              "PowerPoint (*.pptx *.ppt *.pptm *.potx *.ppsx);;OpenDocument (*.odp *.fodp);;All files (*)");
}

QString ShowHost::saveFilterString() const
{
    // D-070: PowerPoint first, so Save As defaults to the format that
    // opens in Microsoft Office and carries the whole deck.
    return tr("PowerPoint Presentation (*.pptx);;OpenDocument Presentation (*.odp);;"
              "Flat OpenDocument Presentation (*.fodp)");
}

void ShowHost::applyPreferences()
{
    emit zoomRequested(Quick::Preferences::instance().defaultZoomPercent() / 100.0);
    updateSlideStatus();
}

void ShowHost::documentReplaced()
{
    m_currentRow = slideCount() > 0 ? 0 : -1;
    m_canvas->clearSelection();
    fullRefresh();
}

Quick::PrintRenderCallback ShowHost::printRenderCallback() const
{
    const ShowModel::Presentation *deck = m_document->presentation();
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
            const qreal scale = qMin(page.width() / slide.width(), page.height() / slide.height());
            painter->save();
            painter->translate(page.center() - QPointF(slide.width() * scale / 2.0, slide.height() * scale / 2.0));
            painter->scale(scale, scale);
            ShowModel::renderSlide(painter, *deck, deck->slides.at(i));
            painter->restore();
        }
    };
}

} // namespace QindaOffice::Apps::Show
