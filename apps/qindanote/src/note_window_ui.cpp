// SPDX-License-Identifier: GPL-3.0-or-later
// NoteWindow interaction wiring: the navigator's context menu and the small
// seams the structure commands use, the Edit clipboard commands, the status
// field, PDF export and the AI dock. Split out of note_window.cpp to keep
// both files inside the line budget.
//
// Rationale (D-017): Edit ▸ Cut/Copy/Paste/Select All and AI ▸ Assistant…
// were permanently disabled in QindaNote, and nothing in the window ever
// said which page of which section was open.

#include "note_window.h"

#include "note_page_view.h"
#include "note_structure.h"
#include "notebook.h"

#include "assistant_panel.h"
#include "document_context.h"
#include "model_catalog.h"
#include "preferences.h"
#include "print_helper.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QTreeWidget>

namespace QindaOffice::Apps::Notes {

namespace {

// Context for the assistant: the focused text block's selection, and the
// current page's typed text as the document scope. Ink is not text and is
// deliberately not sent.
class NoteContext final : public Ai::DocumentContextProvider {
public:
    explicit NoteContext(NoteWindow *window)
        : m_window(window)
    {
    }

    [[nodiscard]] Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        if (auto *edit = qobject_cast<QTextEdit *>(QApplication::focusWidget())) {
            out.selection = edit->textCursor().selectedText();
        }
        if (withDocument) {
            const NotePage *page = m_window->currentPage();
            if (page != nullptr) {
                // Ink is not text and is deliberately never sent; the typed
                // blocks live in the envelope's blocksJson, the same place
                // the find bar reads them from.
                QStringList blocks;
                const QJsonArray array =
                    QJsonDocument::fromJson(page->envelope.blocksJson)
                        .object()
                        .value(QStringLiteral("blocks"))
                        .toArray();
                for (const QJsonValue &value : array) {
                    const QJsonObject object = value.toObject();
                    if (object.value(QStringLiteral("type")).toString()
                        == QLatin1String("text")) {
                        blocks.append(object.value(QStringLiteral("text")).toString());
                    }
                }
                out.document = blocks.join(QLatin1Char('\n'));
            }
        }
        return out;
    }

private:
    NoteWindow *m_window;
};

// Replies go into the focused text block; a page with no text block open
// gets a fresh one, so Insert is never a no-op the user cannot explain.
class NoteApplier final : public Ai::EditApplier {
public:
    explicit NoteApplier(NoteWindow *window)
        : m_window(window)
    {
    }

    void insertAtCursor(const QString &text) override
    {
        if (auto *edit = qobject_cast<QTextEdit *>(QApplication::focusWidget())) {
            edit->insertPlainText(text);
            return;
        }
        m_window->pageView()->addTextBlock();
        if (auto *edit = qobject_cast<QTextEdit *>(QApplication::focusWidget())) {
            edit->insertPlainText(text);
        }
    }

    void replaceSelection(const QString &text) override
    {
        if (auto *edit = qobject_cast<QTextEdit *>(QApplication::focusWidget())) {
            edit->textCursor().insertText(text);
            return;
        }
        insertAtCursor(text);
    }

private:
    NoteWindow *m_window;
};

// Rewrites every typed text block and the page title; returns the number of
// occurrences replaced. Ink carries no text, so it is untouched.
int replaceInPage(NotePage &page, const QString &needle, const QString &replacement,
                  Qt::CaseSensitivity sensitivity)
{
    int hits = page.title.count(needle, sensitivity);
    if (hits > 0) {
        page.title.replace(needle, replacement, sensitivity);
        page.envelope.title = page.title;
    }
    QJsonObject root = QJsonDocument::fromJson(page.envelope.blocksJson).object();
    QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    bool touched = false;
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject object = blocks.at(i).toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("text")) {
            continue;
        }
        QString text = object.value(QStringLiteral("text")).toString();
        const int found = text.count(needle, sensitivity);
        if (found == 0) {
            continue;
        }
        text.replace(needle, replacement, sensitivity);
        object.insert(QStringLiteral("text"), text);
        blocks.replace(i, object);
        hits += found;
        touched = true;
    }
    if (touched) {
        root.insert(QStringLiteral("blocks"), blocks);
        page.envelope.blocksJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }
    return hits;
}

} // namespace

void NoteWindow::replaceAcrossNotebook()
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
    // AGENT-GUARD: persist and DETACH the open page first. Its text blocks
    // are live QTextEdits that write themselves back into blocksJson, so a
    // rewrite underneath them would be overwritten by the next keystroke —
    // and the view would still show the pre-replace text.
    flushPersist();
    const QString sectionId = m_currentSectionId;
    const QString pageId = m_currentPageId;
    m_view->setPage(nullptr);

    Notebook *notebook = m_document->notebook();
    int hits = 0;
    for (const NoteSection &constSection : notebook->sections()) {
        NoteSection *section = notebook->sectionById(constSection.id);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            // Pages that were never opened hold nothing in memory; a sweep
            // that skipped them would silently miss most of the notebook.
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            hits += replaceInPage(page, needle, replacement, Qt::CaseSensitive);
        }
    }
    if (hits > 0) {
        m_document->setDirty(true);
    }
    refreshNavigator();
    selectPageById(sectionId, pageId);
    statusBar()->showMessage(hits > 0 ? tr("Replaced %1 occurrence(s)").arg(hits)
                                      : tr("“%1” not found").arg(needle),
                             5000);
}

NotePage *NoteWindow::currentPage() const
{
    return m_document->notebook()->pageById(m_currentSectionId, m_currentPageId);
}

void NoteWindow::refreshNavigator()
{
    rebuildNavigator();
    updatePageStatus();
}

void NoteWindow::selectPageById(const QString &sectionId, const QString &pageId)
{
    selectPage(sectionId, pageId);
    updatePageStatus();
    if (m_structure != nullptr) {
        m_structure->syncFromSelection();
    }
}

void NoteWindow::clearCurrentPage()
{
    // AGENT-GUARD: the view holds a NotePage* into the notebook's vector.
    // Deleting the page while the view still points at it is a use-after-
    // free the next paint would find.
    m_view->setPage(nullptr);
}

void NoteWindow::wireNavigatorInteraction()
{
    m_navigator->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_navigator, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                if (QTreeWidgetItem *item = m_navigator->itemAt(pos)) {
                    m_navigator->setCurrentItem(item);
                }
                m_structure->showNavigatorMenu(m_navigator->mapToGlobal(pos));
            });
    connect(m_navigator, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        updatePageStatus();
        if (m_structure != nullptr) {
            m_structure->syncFromSelection();
        }
    });
    // The initial selection happened inside rebuildNavigator(), before this
    // connection existed, so sync once here or the page commands start out
    // disabled on a page that is open.
    updatePageStatus();
    m_structure->syncFromSelection();

    // Replace was created disabled and never wired, so QindaNote shipped a
    // dead Edit menu entry (D-012).
    m_actions.editReplace->setEnabled(true);
    connect(m_actions.editReplace, &QAction::triggered, this,
            &NoteWindow::replaceAcrossNotebook);
}

void NoteWindow::wireEditActions()
{
    // The clipboard commands act on the focused text block. Ink has no
    // clipboard representation, so with no text block focused they do
    // nothing — which is why they follow focus rather than being enabled
    // unconditionally: a command that is enabled must do something.
    const auto focusedEdit = [] { return qobject_cast<QTextEdit *>(QApplication::focusWidget()); };
    const auto syncEnabled = [this, focusedEdit] {
        const bool haveEdit = focusedEdit() != nullptr;
        m_actions.editCut->setEnabled(haveEdit);
        m_actions.editCopy->setEnabled(haveEdit);
        m_actions.editPaste->setEnabled(haveEdit);
        m_actions.editSelectAll->setEnabled(haveEdit);
    };
    connect(qApp, &QApplication::focusChanged, this, [syncEnabled] { syncEnabled(); });
    syncEnabled();
    connect(m_actions.editCut, &QAction::triggered, this, [focusedEdit] {
        if (QTextEdit *edit = focusedEdit()) {
            edit->cut();
        }
    });
    connect(m_actions.editCopy, &QAction::triggered, this, [focusedEdit] {
        if (QTextEdit *edit = focusedEdit()) {
            edit->copy();
        }
    });
    connect(m_actions.editPaste, &QAction::triggered, this, [focusedEdit] {
        if (QTextEdit *edit = focusedEdit()) {
            edit->paste();
        }
    });
    connect(m_actions.editSelectAll, &QAction::triggered, this, [focusedEdit] {
        if (QTextEdit *edit = focusedEdit()) {
            edit->selectAll();
        }
    });
}

void NoteWindow::updatePageStatus()
{
    Notebook *notebook = m_document->notebook();
    const NoteSection *section = notebook->sectionById(m_currentSectionId);
    QString info;
    if (section == nullptr) {
        info = tr("No page");
    } else {
        int index = 0;
        for (int i = 0; i < section->pages.size(); ++i) {
            if (section->pages.at(i).id == m_currentPageId) {
                index = i + 1;
            }
        }
        // The active tool and its width ride the status field: on a pen
        // surface the toolbar is often hidden, and "which pen, how thick"
        // is the one thing you cannot tell from the page.
        QString tool;
        switch (m_view->tool()) {
        case NoteTool::Pen:
            tool = tr("Pen %1").arg(m_view->penWidth());
            break;
        case NoteTool::Highlighter:
            tool = tr("Highlighter %1").arg(m_view->highlighterWidth());
            break;
        case NoteTool::Eraser:
            tool = tr("Eraser %1").arg(m_view->eraserRadius());
            break;
        case NoteTool::Text:
            tool = tr("Text");
            break;
        }
        info = tr("%1 — page %2 of %3 · %4 · Zoom %5 %")
                   .arg(section->name)
                   .arg(index)
                   .arg(section->pages.size())
                   .arg(tool)
                   .arg(qRound(m_view->zoom() * 100));
    }
    setStatusInfo(info);
}

void NoteWindow::exportPdf()
{
    const QString suggestion =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/')
        + m_document->displayTitle() + QStringLiteral(".pdf");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), suggestion,
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    flushPersist();
    // The SAME paginating callback Print and Print Preview use, so paper,
    // preview and PDF cannot drift apart (plan 5.2).
    if (Shell::PrintHelper::exportPdf(this, path, printRenderCallback())) {
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Export failed: could not write %1").arg(path), 8000);
    }
}

void NoteWindow::wireAssistant()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/qindaoffice/QindaNote");
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"));
    m_catalog->setParent(this);
    m_panel = new Ai::AssistantPanel(m_catalog, new NoteContext(this), new NoteApplier(this), this);
    auto *dock = new QDockWidget(tr("Assistant"), this);
    dock->setObjectName(QStringLiteral("assistantDock"));
    dock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    m_actions.aiAssistant->setEnabled(true);
    m_actions.aiAssistant->setCheckable(true);
    connect(m_actions.aiAssistant, &QAction::toggled, dock, &QDockWidget::setVisible);
    connect(dock, &QDockWidget::visibilityChanged, m_actions.aiAssistant, &QAction::setChecked);
}

void NoteWindow::applyPreferences()
{
    const Shell::Preferences &prefs = Shell::Preferences::instance();
    m_view->setZoom(prefs.defaultZoomPercent() / 100.0);
    // The Notes page's pen behaviour (D-014: every key is read by exactly
    // this code).
    m_view->setPressureEnabled(prefs.notePressureEnabled());
    const QString smoothing = prefs.noteSmoothing();
    m_view->setStrokeSmoothing(smoothing == QLatin1String("off") ? 0.0
                               : smoothing == QLatin1String("strong") ? 0.5
                                                                      : 0.25);
    updatePageStatus();
}

} // namespace QindaOffice::Apps::Notes
