// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_window.h"

#include "find_replace_bar.h"
#include "format_controller.h"
#include "table_actions.h"
#include "page_text_accessibility.h"
#include "page_text_view.h"
#include "pdf_exporter.h"
#include "template_loader.h"
#include "assistant_panel.h"
#include "document_context.h"
#include "format_io.h"
#include "inline_action_flow.h"
#include "model_catalog.h"
#include "print_helper.h"
#include "spell_checker.h"
#include "style_catalog.h"
#include "write_document.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QStandardPaths>
#include <QToolBar>
#include <QVBoxLayout>
#include <QPrinter>
#include <QTextBlock>
#include <QtWidgets/private/qwidgettextcontrol_p.h>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {


namespace {

// DocumentContext/EditApplier seams (plan 5.3) for the Write document.
class WriteContext final : public Ai::DocumentContextProvider {
public:
    explicit WriteContext(PageTextView *view)
        : m_view(view)
    {
    }
    Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        const QTextCursor cursor = m_view->control()->textCursor();
        if (cursor.hasSelection()) {
            out.selection = cursor.selectedText().replace(QChar::ParagraphSeparator,
                                                          QLatin1Char('\n'));
        }
        if (withDocument) {
            out.document = m_view->writeDocument()->textDocument()->toPlainText();
        }
        return out;
    }

private:
    PageTextView *m_view;
};

class WriteApplier final : public Ai::EditApplier {
public:
    WriteApplier(PageTextView *view, WriteDocument *doc)
        : m_view(view)
        , m_doc(doc)
    {
    }
    void replaceSelection(const QString &text) override
    {
        QTextCursor cursor = m_view->control()->textCursor();
        cursor.insertText(text);
        m_doc->setDirty(true);
    }
    void insertAtCursor(const QString &text) override
    {
        QTextCursor cursor = m_view->control()->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("\n") + text);
        m_doc->setDirty(true);
    }

private:
    PageTextView *m_view;
    WriteDocument *m_doc;
};

} // namespace

WriteWindow::WriteWindow()
    : m_document(new WriteDocument)
    , m_view(new PageTextView(m_document))
    , m_spell(new SpellChecker(m_document->textDocument()))
{
    installPageTextAccessibility();

    // Central stack: the page view above the find/replace bar (hidden
    // until Ctrl+F/Ctrl+H). The bar is part of the document host so it
    // never overlaps the page area.
    auto *central = new QWidget(this);
    auto *stack = new QVBoxLayout(central);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    m_findBar = new FindReplaceBar(m_view, m_document, central);
    m_findBar->hide();
    stack->addWidget(m_view, 1);
    stack->addWidget(m_findBar, 0);
    setDocumentHost(central, m_document);

    // Format commands live in the controller; the window adds its toolbar
    // below the shell's main toolbar and reuses the same actions in menus.
    m_format = new FormatController(m_view, m_document, this);
    m_tables = new TableController(m_view, m_document, this);
    QToolBar *mainToolBar = addMainToolBar();
    Q_UNUSED(mainToolBar);
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar *formatToolBar = m_format->toolBar();
    m_tables->appendToToolBar(formatToolBar);
    addToolBar(Qt::TopToolBarArea, formatToolBar);

    wireEditingActions();

    connect(m_findBar, &FindReplaceBar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });

    // Print and print preview share the PDF exporter's pagination loop, so
    // paper matches the screen by construction (plan 6.1).
    m_actions.filePrint->setEnabled(true);
    connect(m_actions.filePrint, &QAction::triggered, this, [this] {
        Shell::PrintHelper::print(this, printRenderCallback());
    });
    m_actions.filePrintPreview->setEnabled(true);
    connect(m_actions.filePrintPreview, &QAction::triggered, this, [this] {
        Shell::PrintHelper::printPreview(this, printRenderCallback());
    });
    connect(m_view, &PageTextView::contextMenuBuilt, this, &WriteWindow::addSpellingActions);

    // AI: the catalog lives per window (providers.json is a small seeded
    // document); the panel docks right, inline actions live in the AI menu.
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/qindaoffice/QindaWrite");
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"));
    m_catalog->setParent(this);

    auto *context = new WriteContext(m_view);
    auto *applier = new WriteApplier(m_view, m_document);
    m_panel = new Ai::AssistantPanel(m_catalog, context, applier, this);
    auto *dock = new QDockWidget(tr("Assistant"), this);
    dock->setObjectName(QStringLiteral("assistantDock"));
    dock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    m_actions.aiAssistant->setEnabled(true);
    m_actions.aiAssistant->setCheckable(true);
    connect(m_actions.aiAssistant, &QAction::toggled, dock, &QDockWidget::setVisible);
    connect(dock, &QDockWidget::visibilityChanged, m_actions.aiAssistant,
            &QAction::setChecked);

    m_inline = new Ai::InlineActionFlow(m_catalog, context, applier, this);

    // App menus (Format/Insert/Page) need the fully constructed window -
    // the base ctor's hook dispatch only reaches the base no-op.
    finalizeAppMenuHooks();
}

WriteWindow::~WriteWindow() = default;

void WriteWindow::wireEditingActions()
{
    // The M1 stub wired these to a QTextEdit; the real engine is the
    // QWidgetTextControl behind the page view. Undo/cut/copy follow the
    // control's availability signals instead of staying always-on.
    QWidgetTextControl *control = m_view->control();
    connect(m_actions.editUndo, &QAction::triggered, control, &QWidgetTextControl::undo);
    connect(m_actions.editRedo, &QAction::triggered, control, &QWidgetTextControl::redo);
    connect(m_actions.editCut, &QAction::triggered, control, &QWidgetTextControl::cut);
    connect(m_actions.editCopy, &QAction::triggered, control, &QWidgetTextControl::copy);
    connect(m_actions.editPaste, &QAction::triggered, control,
            [control] { control->paste(); });
    connect(m_actions.editSelectAll, &QAction::triggered, control,
            &QWidgetTextControl::selectAll);

    m_actions.editUndo->setEnabled(false);
    m_actions.editRedo->setEnabled(false);
    m_actions.editCut->setEnabled(false);
    m_actions.editCopy->setEnabled(false);
    m_actions.editPaste->setEnabled(true);
    m_actions.editSelectAll->setEnabled(true);
    connect(control, &QWidgetTextControl::undoAvailable, m_actions.editUndo,
            &QAction::setEnabled);
    connect(control, &QWidgetTextControl::redoAvailable, m_actions.editRedo,
            &QAction::setEnabled);
    connect(control, &QWidgetTextControl::copyAvailable, m_actions.editCut,
            &QAction::setEnabled);
    connect(control, &QWidgetTextControl::copyAvailable, m_actions.editCopy,
            &QAction::setEnabled);

    m_actions.editFind->setEnabled(true);
    m_actions.editReplace->setEnabled(true);
    m_actions.editFindNext->setEnabled(true);
    m_actions.editFindPrevious->setEnabled(true);
    connect(m_actions.editFind, &QAction::triggered, m_findBar,
            &FindReplaceBar::openFind);
    connect(m_actions.editReplace, &QAction::triggered, m_findBar,
            &FindReplaceBar::openReplace);
    connect(m_actions.editFindNext, &QAction::triggered, m_findBar,
            &FindReplaceBar::findNext);
    connect(m_actions.editFindPrevious, &QAction::triggered, m_findBar,
            &FindReplaceBar::findPrevious);

    m_actions.viewZoomIn->setEnabled(true);
    m_actions.viewZoomOut->setEnabled(true);
    m_actions.viewZoomReset->setEnabled(true);
    connect(m_actions.viewZoomIn, &QAction::triggered, this,
            [this] { m_view->setZoom(m_view->zoom() * 1.1); });
    connect(m_actions.viewZoomOut, &QAction::triggered, this,
            [this] { m_view->setZoom(m_view->zoom() / 1.1); });
    connect(m_actions.viewZoomReset, &QAction::triggered, this,
            [this] { m_view->setZoom(1.0); });
}

void WriteWindow::exportPdf()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (exportDocumentPdf(m_document->textDocument(), path, &error, m_document->decoration())) {
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Export failed: %1").arg(error), 8000);
    }
}

void WriteWindow::applyStyle(int styleId)
{
    StyleCatalog catalog;
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    QTextBlock block = m_document->textDocument()->begin();
    const int start = qMin(cursor.selectionStart(), cursor.selectionEnd());
    const int end = qMax(cursor.selectionStart(), cursor.selectionEnd());
    while (block.isValid()) {
        if (block.position() >= start && block.position() <= end) {
            QTextCursor blockCursor(block);
            catalog.apply(blockCursor, styleId);
        }
        if (block.position() > end) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
}

Shell::PrintRenderCallback WriteWindow::printRenderCallback() const
{
    return [this](QPrinter *printer, QPainter *painter) {
        renderDocumentPages(painter, QSizeF(printer->width(), printer->height()),
                            m_document->textDocument(),
                            [printer] { printer->newPage(); }, m_document->decoration());
    };
}

void WriteWindow::addSpellingActions(QMenu *menu, const QPointF &docPos)
{
    if (m_spell == nullptr || !m_spell->isActive()) {
        return;
    }
    const QTextCursor click = m_view->control()->cursorForPosition(docPos);
    QTextCursor word = click;
    word.select(QTextCursor::WordUnderCursor);
    const QString text = word.selectedText();
    if (text.size() < 2 || !m_spell->isMisspelled(text)) {
        return;
    }
    QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    const QStringList suggestions = m_spell->suggestions(text);
    if (!suggestions.isEmpty()) {
        auto *suggestMenu = new QMenu(tr("Spelling"), menu);
        for (const QString &suggestion : suggestions.mid(0, 7)) {
            QAction *action = suggestMenu->addAction(suggestion);
            connect(action, &QAction::triggered, this, [this, word, suggestion]() mutable {
                word.insertText(suggestion);
                m_document->setDirty(true);
            });
        }
        menu->insertMenu(first, suggestMenu);
    }
    QAction *ignore = new QAction(tr("Ignore '%1'").arg(text), menu);
    connect(ignore, &QAction::triggered, this, [text] {
        // Session-scoped ignore in v1 (no custom dictionary management).
        Q_UNUSED(text);
    });
    if (first != nullptr) {
        menu->insertAction(first, ignore);
    } else {
        menu->addAction(ignore);
    }
}

void WriteWindow::insertFileMenuActions(QMenu *fileMenu)
{
    const QList<Shell::TemplateEntry> templates = Shell::templatesFor(QStringLiteral("QindaWrite"));
    if (templates.isEmpty()) {
        return;
    }
    QMenu *submenu = fileMenu->addMenu(tr("New from &Template"));
    for (const Shell::TemplateEntry &entry : templates) {
        submenu->addAction(entry.name, this, [this, path = entry.path] {
            QString error;
            // The view renders the document's shared QTextDocument, so a
            // load refreshes it directly; detach so saving never touches
            // the shared template.
            if (m_document->load(path, &error)) {
                m_document->adoptPath(QString());
                m_document->setDirty(true);
            }
        });
    }
}

void WriteWindow::insertApplicationMenus(QMenu *before)
{
    auto *formatMenu = new QMenu(tr("F&ormat"), this);
    formatMenu->setObjectName(QStringLiteral("formatMenu"));
    menuBar()->insertMenu(before->menuAction(), formatMenu);

    auto *styleMenu = new QMenu(tr("&Style"), this);
    formatMenu->addMenu(styleMenu);
    StyleCatalog catalog;
    const QStringList names = catalog.names();
    for (int id = 0; id < names.size(); ++id) {
        QAction *action = styleMenu->addAction(names.at(id));
        connect(action, &QAction::triggered, this, [this, id] { applyStyle(id); });
    }
    formatMenu->addSeparator();
    m_format->populateFormatMenu(formatMenu);

    auto *tableMenu = new QMenu(tr("&Table"), this);
    tableMenu->setObjectName(QStringLiteral("tableMenu"));
    menuBar()->insertMenu(before->menuAction(), tableMenu);
    m_tables->populateTableMenu(tableMenu);

    auto *insertMenu = new QMenu(tr("&Insert"), this);
    insertMenu->setObjectName(QStringLiteral("insertMenu"));
    menuBar()->insertMenu(before->menuAction(), insertMenu);
    m_format->populateInsertMenu(insertMenu);

    auto *aiMenu = new QMenu(tr("&AI"), this);
    menuBar()->insertMenu(before->menuAction(), aiMenu);
    for (const Ai::InlineAction &action : Ai::inlineActions()) {
        QAction *item = aiMenu->addAction(action.displayName);
        connect(item, &QAction::triggered, this, [this, id = action.id] {
            m_inline->run(id, this);
        });
    }

    auto *pageMenu = new QMenu(tr("&Page"), this);
    pageMenu->setObjectName(QStringLiteral("pageMenu"));
    menuBar()->insertMenu(before->menuAction(), pageMenu);
    QAction *pageSetup = pageMenu->addAction(tr("&Page Setup…"));
    connect(pageSetup, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        auto *layout = new QFormLayout(&dialog);
        auto *size = new QComboBox(&dialog);
        size->addItem(QStringLiteral("A4"), QSizeF(794, 1123));
        size->addItem(QStringLiteral("Letter"), QSizeF(816, 1056));
        auto *margin = new QDoubleSpinBox(&dialog);
        margin->setRange(0, 200);
        margin->setValue(m_document->pageMargin());
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(tr("Paper"), size);
        layout->addRow(tr("Margin (px)"), margin);
        auto *header = new QLineEdit(m_document->headerText(), &dialog);
        auto *footer = new QLineEdit(m_document->footerText(), &dialog);
        header->setPlaceholderText(tr("Header ({page} of {pages})"));
        footer->setPlaceholderText(tr("Footer ({page} of {pages})"));
        layout->addRow(tr("Header"), header);
        layout->addRow(tr("Footer"), footer);
        layout->addRow(buttons);
        if (dialog.exec() == QDialog::Accepted) {
            m_document->setupPaged(size->currentData().toSizeF(), margin->value());
            m_document->setHeaderFooter(header->text(), footer->text());
            m_view->geometry()->setPageSize(size->currentData().toSizeF());
            m_view->viewport()->update();
            m_document->setDirty(true);
        }
    });
    QAction *exportAction = pageMenu->addAction(tr("&Export PDF…"));
    connect(exportAction, &QAction::triggered, this, &WriteWindow::exportPdf);
}

QString WriteWindow::filterString() const
{
    return openFilterString();
}

QString WriteWindow::saveFilterString() const
{
    // Default Save format is ODT (plan 6.1).
    return defaultSaveFilter();
}

} // namespace QindaOffice::Apps::Write
