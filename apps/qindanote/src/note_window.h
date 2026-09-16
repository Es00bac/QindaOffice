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

class QToolButton;
class QTreeWidget;

namespace QindaOffice::Apps::Notes {

class NotePageView;

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

protected:
    void insertApplicationMenus(QMenu *before) override;
    QString filterString() const override;

private:
    void rebuildNavigator();
    void persistCurrentPage();
    // AGENT-GUARD: the window holds section/page IDS, never pointers — the
    // notebook's QVectors reallocate on every append/load and a stale
    // pointer was use-after-free (and once wrote garbage-named page files).
    void setCurrentIds(const QString &sectionId, const QString &pageId);
    // Paginating renderer shared by Print and Print Preview: one notebook
    // page per sheet of paper, through the same pure renderer the screen
    // agrees with.
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;

    NoteDocument *m_document;
    NotePageView *m_view;
    QTreeWidget *m_navigator;
    QString m_currentSectionId;
    QString m_currentPageId;
};

} // namespace QindaOffice::Apps::Notes
