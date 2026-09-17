// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaNote window (plan section 6.4): notebook navigator sidebar
// (sections and pages), the note page surface, tool bar (pen/highlighter/
// eraser/text), and notebook persistence through the Core::Document
// interface (a notebook directory). One notebook = one window.

#include "document.h"
#include "document_window.h"
#include "notebook.h"
#include "print_helper.h"

class QScrollArea;
class QToolButton;
class QTreeWidget;

namespace QindaOffice::Ai {
class AssistantPanel;
class ModelCatalog;
}

namespace QindaOffice::Apps::Notes {

class NotePageView;
class NoteFindBar;
class NoteStructureActions;

class NoteDocument final : public Core::Document {
    Q_OBJECT
public:
    NoteDocument();
    [[nodiscard]] Notebook *notebook() { return &m_notebook; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override
    {
        return m_notebook.toRecoveryJson();
    }
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override
    {
        if (!m_notebook.fromRecoveryJson(snapshot, error)) {
            return false;
        }
        setCanonicalPath(m_notebook.directory());
        refreshByteRevision();
        setDirty(true); // recovered content is unsaved by definition
        emit contentChanged();
        return true;
    }

private:
    Notebook m_notebook;
};

class NoteWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    NoteWindow();
    ~NoteWindow() override;

    [[nodiscard]] NoteDocument *noteDocument() const { return m_document; }
    [[nodiscard]] NotePageView *pageView() const { return m_view; }
    void openNotebookPath(const QString &path);

    // Seams for NoteStructureActions (note_structure.cpp). The window owns
    // "which page is open"; the structure commands ask rather than keep
    // their own copy, because the notebook's vectors reallocate freely and
    // a second copy would go stale (AGENT-GUARD, see setCurrentIds).
    [[nodiscard]] const QString &currentSectionId() const { return m_currentSectionId; }
    [[nodiscard]] const QString &currentPageId() const { return m_currentPageId; }
    [[nodiscard]] NotePage *currentPage() const;
    void refreshNavigator();
    void selectPageById(const QString &sectionId, const QString &pageId);
    // Detaches the view from the current page before the page is destroyed.
    void clearCurrentPage();
    [[nodiscard]] NoteStructureActions *structureActions() const { return m_structure; }

protected:
    void insertApplicationMenus(QMenu *before) override;
    void applyPreferences() override;
    QString filterString() const override;

private:
    // Status bar, navigator context menu, edit commands and the AI dock
    // (note_window_ui.cpp).
    void wireNavigatorInteraction();
    void wireEditActions();
    void wireAssistant();
    void updatePageStatus();
    void exportPdf();
    // Edit ▸ Replace across the whole notebook (page titles and typed text
    // blocks) — pages that were never opened are read from disk first.
    void replaceAcrossNotebook();
    void rebuildNavigator();
    void persistCurrentPage();
    // AGENT-GUARD: the window holds section/page IDS, never pointers — the
    // notebook's QVectors reallocate on every append/load and a stale
    // pointer was use-after-free (and once wrote garbage-named page files).
    void setCurrentIds(const QString &sectionId, const QString &pageId);
    // Selects a page in the navigator by id, which drives the existing
    // currentItemChanged path (load, persist, show). Used by the find bar.
    void selectPage(const QString &sectionId, const QString &pageId);
    // Paginating renderer shared by Print and Print Preview: one notebook
    // page per sheet of paper, through the same pure renderer the screen
    // agrees with.
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;

    NoteDocument *m_document;
    NotePageView *m_view;
    QTreeWidget *m_navigator;
    NoteFindBar *m_findBar = nullptr;
    NoteStructureActions *m_structure = nullptr;
    class QScrollArea *m_scroll = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantPanel *m_panel = nullptr;
    QString m_currentSectionId;
    QString m_currentPageId;
};

} // namespace QindaOffice::Apps::Notes
