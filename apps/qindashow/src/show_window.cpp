// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_window.h"

#include "assistant_panel.h"
#include "inline_action_flow.h"
#include "model_catalog.h"
#include "pdf_export.h"
#include "presentation_controller.h"
#include "presenter_window.h"
#include "show_ai.h"
#include "show_commands.h"
#include "show_toolbox.h"
#include "slide_canvas.h"
#include "slide_player.h"
#include "slide_renderer.h"

#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QDir>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScreen>
#include <QToolBar>

#include "standard_actions.h"
#include "wrapping_tool_bar.h"
#include <QStandardPaths>
#include <QStatusBar>
#include <QWindow>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>

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
    const QRectF titleRect(size.width() * 0.1, size.height() * 0.08, size.width() * 0.8,
                           size.height() * 0.18);
    if (layout == QLatin1String("title")) {
        textBox(titleRect, title, 44, true);
    } else if (layout == QLatin1String("title+content")) {
        textBox(titleRect, title, 36, true);
        textBox(QRectF(size.width() * 0.1, size.height() * 0.35, size.width() * 0.8,
                       size.height() * 0.5),
                QString(), 24, false);
    } else if (layout == QLatin1String("title+2content") || layout == QLatin1String("two-content")) {
        textBox(titleRect, title, 36, true);
        textBox(QRectF(size.width() * 0.06, size.height() * 0.35, size.width() * 0.42,
                       size.height() * 0.5),
                QString(), 24, false);
        textBox(QRectF(size.width() * 0.52, size.height() * 0.35, size.width() * 0.42,
                       size.height() * 0.5),
                QString(), 24, false);
    } else if (layout == QLatin1String("content")) {
        textBox(QRectF(size.width() * 0.1, size.height() * 0.15, size.width() * 0.8,
                       size.height() * 0.7),
                QString(), 24, false);
    }
    return slide; // "blank" gets no shapes
}

} // namespace

ShowWindow::ShowWindow()
    : m_document(new ShowDocument)
    , m_canvas(new SlideCanvas)
    , m_sorter(new QListWidget)
    , m_notes(new QPlainTextEdit)
    , m_transition(new QComboBox)
    , m_layout(new QComboBox)
{
    setDocumentHost(m_canvas, m_document);
    setWindowTitle(tr("QindaShow"));

    m_sorter->setViewMode(QListWidget::IconMode);
    m_sorter->setIconSize(QSize(160, 90));
    m_sorter->setResizeMode(QListWidget::Adjust);
    m_sorter->setMovement(QListWidget::Static);
    m_sorter->setDragDropMode(QAbstractItemView::InternalMove);
    m_sorter->setDefaultDropAction(Qt::MoveAction);
    m_sorter->setWrapping(true);
    m_sorter->setMinimumWidth(190);
    auto *sorterDock = new QDockWidget(tr("Slides"), this);
    sorterDock->setObjectName(QStringLiteral("slideSorter"));
    sorterDock->setWidget(m_sorter);
    addDockWidget(Qt::LeftDockWidgetArea, sorterDock);

    auto *metaPanel = new QWidget(this);
    auto *metaLayout = new QVBoxLayout(metaPanel);
    metaLayout->setContentsMargins(6, 6, 6, 6);
    auto *form = new QFormLayout;
    m_transition->addItems({tr("Cut"), tr("Fade"), tr("Slide"), tr("Zoom")});
    m_layout->addItems({QStringLiteral("title"), QStringLiteral("title+content"),
                        QStringLiteral("title+2content"), QStringLiteral("content"),
                        QStringLiteral("two-content"), QStringLiteral("blank")});
    form->addRow(tr("Transition"), m_transition);
    form->addRow(tr("Layout"), m_layout);
    metaLayout->addLayout(form);
    metaLayout->addWidget(new QLabel(tr("Speaker notes"), this));
    metaLayout->addWidget(m_notes, 1);
    auto *metaDock = new QDockWidget(tr("Notes and slide"), this);
    metaDock->setObjectName(QStringLiteral("notesDock"));
    metaDock->setWidget(metaPanel);
    addDockWidget(Qt::RightDockWidgetArea, metaDock);

    connect(m_sorter, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_syncingMeta) {
            switchSlide(row);
        }
    });
    m_sortSyncing = false;
    connect(m_sorter->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (m_sortSyncing) {
            return;
        }
        m_sortSyncing = true;
        // Reorder the model to match the sorter (single user drag).
        ShowModel::Presentation *deck = m_document->presentation();
        QStringList order;
        for (int i = 0; i < m_sorter->count(); ++i) {
            order.append(m_sorter->item(i)->data(Qt::UserRole).toString());
        }
        QList<ShowModel::Slide> reordered;
        for (const QString &id : order) {
            if (ShowModel::Slide *slide = deck->findSlide(id)) {
                reordered.append(*slide);
            }
        }
        if (reordered.size() == deck->slides.size()) {
            // AGENT-GUARD: mutate first, then push. QUndoStack::push runs
            // redo() synchronously → indexChanged → fullRefresh(); pushing
            // before the mutation refreshed against the OLD list (visually
            // reverting the drag), then swapping the model left sorter and
            // deck permanently desynced — clicks/deletes hit the wrong
            // slide and canvas edits landed on a detached slide buffer.
            const QList<ShowModel::Slide> before = deck->slides;
            deck->slides = reordered;
            pushSlideListCommand(before);
            m_document->setDirty(true);
            m_document->notifyContentChanged();
        } else {
            // Duplicate/empty slide ids (imported decks): rebuilding the
            // sorter from the deck is the only consistent recovery; a
            // partial reorder would drop slides.
            rebuildSorter();
        }
        m_sortSyncing = false;
    });
    connect(m_notes, &QPlainTextEdit::textChanged, this, [this] {
        if (m_syncingMeta || m_currentRow < 0) {
            return;
        }
        ShowModel::Presentation *deck = m_document->presentation();
        if (m_currentRow < deck->slides.size()) {
            deck->slides[m_currentRow].notes = m_notes->toPlainText();
            m_document->setDirty(true);
            m_document->notifyContentChanged();
        }
    });
    connect(m_transition, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_syncingMeta || m_currentRow < 0) {
            return;
        }
        ShowModel::Presentation *deck = m_document->presentation();
        if (m_currentRow < deck->slides.size()) {
            static const char *kTransitions[] = {"cut", "fade", "slide", "zoom"};
            deck->slides[m_currentRow].transition = QLatin1String(kTransitions[index]);
            m_document->setDirty(true);
            m_document->notifyContentChanged();
        }
    });
    connect(m_layout, &QComboBox::currentTextChanged, this, [this](const QString &layout) {
        if (m_syncingMeta || m_currentRow < 0) {
            return;
        }
        ShowModel::Presentation *deck = m_document->presentation();
        if (m_currentRow < deck->slides.size()) {
            deck->slides[m_currentRow].layout = layout;
            m_document->setDirty(true);
            m_document->notifyContentChanged();
        }
    });

    // Rewire the shell undo/redo actions to the document stack (the M0
    // connections point at the replaced stub editor and are inert).
    connect(standardActions()->editUndo, &QAction::triggered, this,
            [this] { m_document->undoStack()->undo(); });
    connect(standardActions()->editRedo, &QAction::triggered, this,
            [this] { m_document->undoStack()->redo(); });
    connect(m_document->undoStack(), &QUndoStack::indexChanged, this,
            [this] { fullRefresh(); });

    // AI: catalog per window (same contract as QindaWrite); panel docks
    // right; inline actions and deck generation live in the AI menu.
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/qindaoffice/QindaShow");
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"));
    m_catalog->setParent(this);
    auto *context = new ShowContext(this);
    auto *applier = new ShowApplier(this);
    m_panel = new Ai::AssistantPanel(m_catalog, context, applier, this);
    auto *assistantDock = new QDockWidget(tr("Assistant"), this);
    assistantDock->setObjectName(QStringLiteral("assistantDock"));
    assistantDock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, assistantDock);
    assistantDock->hide();
    m_actions.aiAssistant->setEnabled(true);
    m_actions.aiAssistant->setCheckable(true);
    connect(m_actions.aiAssistant, &QAction::toggled, assistantDock, &QDockWidget::setVisible);
    connect(assistantDock, &QDockWidget::visibilityChanged, m_actions.aiAssistant,
            &QAction::setChecked);
    m_inline = new Ai::InlineActionFlow(m_catalog, context, applier, this);

    // Persistent named slide actions (action discipline, AGENTS.md rule 5),
    // shared by the Slide menu and the main toolbar.
    using Shell::ActionId::kSlideNew;
    using Shell::ActionId::kSlideDuplicate;
    using Shell::ActionId::kSlideDelete;
    using Shell::ActionId::kSlideStartShow;
    m_newSlideAction = new QAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                   tr("New slide"), this);
    m_newSlideAction->setObjectName(QString::fromLatin1(kSlideNew));
    m_newSlideAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(m_newSlideAction, &QAction::triggered, this, [this] { addSlide(); });
    m_duplicateSlideAction =
        new QAction(QIcon::fromTheme(QStringLiteral("edit-duplicate")),
                    tr("Duplicate slide"), this);
    m_duplicateSlideAction->setObjectName(QString::fromLatin1(kSlideDuplicate));
    connect(m_duplicateSlideAction, &QAction::triggered, this, [this] { duplicateSlide(); });
    m_deleteSlideAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                      tr("Delete slide"), this);
    m_deleteSlideAction->setObjectName(QString::fromLatin1(kSlideDelete));
    connect(m_deleteSlideAction, &QAction::triggered, this, [this] { deleteSlide(); });
    m_startShowAction =
        new QAction(QIcon::fromTheme(QStringLiteral("media-playback-start")),
                    tr("Start show"), this);
    m_startShowAction->setObjectName(QString::fromLatin1(kSlideStartShow));
    m_startShowAction->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(m_startShowAction, &QAction::triggered, this, [this] { startShow(); });

    Shell::WrappingToolBar *mainToolBar = addMainToolBar();
    mainToolBar->addSeparator();
    mainToolBar->addAction(m_newSlideAction);
    mainToolBar->addAction(m_duplicateSlideAction);
    mainToolBar->addAction(m_deleteSlideAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(m_startShowAction);

    // The toolbox must exist BEFORE finalizeAppMenuHooks: the app menus
    // reuse its persistent insert/style actions (AGENT-GUARD: the hook
    // runs insertApplicationMenus, which dereferences m_toolbox).
    m_toolbox = new ShowToolbox(m_canvas, this);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_toolbox->toolBar());

    // The base ctor ran createMenus() before this subclass existed, so the
    // app-menu hooks only dispatched the base no-ops then; run them now
    // (shell contract: app menus between View and AI).
    finalizeAppMenuHooks();

    wireCanvasUndo();
    wireShowEditingActions();
    connect(m_document, &ShowDocument::loaded, this, [this] { fullRefresh(); });
    connect(m_canvas, &SlideCanvas::slideEdited, this, [this] {
        m_document->setDirty(true);
        m_document->notifyContentChanged();
    });

    fullRefresh();
}

ShowWindow::~ShowWindow()
{
    // The document (and its undo stack) is a child that outlives this
    // dtor body but is destroyed while the QWidget base is tearing down;
    // a command destroyed with the stack would re-enter fullRefresh()
    // through the indexChanged lambda on a half-dead window.
    disconnect(m_document->undoStack(), nullptr, this, nullptr);
}


void ShowWindow::fullRefresh()
{
    rebuildSorter();
    if (m_currentRow < 0 && m_sorter->count() > 0) {
        m_currentRow = 0;
    }
    if (m_currentRow >= 0 && m_currentRow < m_sorter->count()) {
        m_sorter->setCurrentRow(m_currentRow);
        switchSlide(m_currentRow);
    } else {
        m_canvas->setSlide(nullptr, m_document->presentation()->slideSize);
        m_currentRow = -1;
    }
}

void ShowWindow::rebuildSorter()
{
    m_syncingMeta = true;
    ShowModel::Presentation *deck = m_document->presentation();
    m_sorter->clear();
    for (const ShowModel::Slide &slide : deck->slides) {
        const QPixmap thumbnail = ShowModel::renderSlideToPixmap(slide, deck->slideSize,
                                                            QSize(160, 90));
        auto *item = new QListWidgetItem(QIcon(thumbnail), slide.title);
        item->setData(Qt::UserRole, slide.id);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        m_sorter->addItem(item);
    }
    m_syncingMeta = false;
}

void ShowWindow::switchSlide(int row)
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (row < 0 || row >= deck->slides.size()) {
        return;
    }
    m_currentRow = row;
    m_canvas->setSlide(&deck->slides[row], deck->slideSize);
    syncMetaWidgets();
}

void ShowWindow::syncMetaWidgets()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size()) {
        return;
    }
    const ShowModel::Slide &slide = deck->slides.at(m_currentRow);
    m_syncingMeta = true;
    m_notes->setPlainText(slide.notes);
    static const char *kTransitions[] = {"cut", "fade", "slide", "zoom"};
    int index = 0;
    for (int i = 0; i < 4; ++i) {
        if (slide.transition == QLatin1String(kTransitions[i])) {
            index = i;
        }
    }
    m_transition->setCurrentIndex(index);
    const int layoutIndex = m_layout->findText(slide.layout);
    m_layout->setCurrentIndex(layoutIndex >= 0 ? layoutIndex : 0);
    m_syncingMeta = false;
}

void ShowWindow::pushSlideListCommand(const QList<ShowModel::Slide> &before)
{
    ShowModel::Presentation *deck = m_document->presentation();
    m_document->undoStack()->push(new SlideListCommand(deck, before, deck->slides));
}

void ShowWindow::newDeck()
{
    ShowModel::Presentation *deck = m_document->presentation();
    // AGENT-GUARD: mutate first, then push. QUndoStack::push runs redo()
    // synchronously, which fires indexChanged → fullRefresh(); the refresh
    // must observe the NEW state, and the command's after-image must be the
    // new list (pushing before the mutation captured the old list twice,
    // which made redo a silent no-op that resurrected stale undo state).
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.clear();
    deck->addSlide(tr("Slide 1")).layout = QStringLiteral("title");
    pushSlideListCommand(before);
    m_currentRow = 0;
    m_document->setDirty(true);
    fullRefresh();
}

void ShowWindow::addSlide()
{
    ShowModel::Presentation *deck = m_document->presentation();
    const QString layout = m_layout->currentText().isEmpty() ? QStringLiteral("title+content")
                                                             : m_layout->currentText();
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.append(makeSlide(tr("Slide %1").arg(deck->slides.size() + 1), layout,
                                  deck->slideSize));
    pushSlideListCommand(before);
    m_currentRow = deck->slides.size() - 1;
    m_document->setDirty(true);
    fullRefresh();
}

void ShowWindow::duplicateSlide()
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
    pushSlideListCommand(before);
    m_currentRow += 1;
    m_document->setDirty(true);
    fullRefresh();
}

void ShowWindow::deleteSlide()
{
    ShowModel::Presentation *deck = m_document->presentation();
    if (m_currentRow < 0 || m_currentRow >= deck->slides.size()) {
        return;
    }
    const QList<ShowModel::Slide> before = deck->slides;
    deck->slides.removeAt(m_currentRow);
    pushSlideListCommand(before);
    if (m_currentRow >= deck->slides.size()) {
        m_currentRow = deck->slides.size() - 1;
    }
    m_document->setDirty(true);
    fullRefresh();
}


QString ShowWindow::filterString() const
{
    return tr("QindaShow presentations (*.fodp *.odp *.pptx);;All files (*)");
}

} // namespace QindaOffice::Apps::Show
