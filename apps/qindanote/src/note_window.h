// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaNote window (plan section 6.4): notebook navigator sidebar
// (sections and pages), the note page surface, tool bar (pen/highlighter/
// eraser/text), and notebook persistence through the Core::Document
// interface (a notebook directory). One notebook = one window.

#include "document.h"
#include "document_window.h"
#include "notebook.h"

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
    [[nodiscard]] QByteArray recoverySnapshot() const override { return {}; }
    bool loadRecovery(const QByteArray &, QString *error = nullptr) override
    {
        if (error != nullptr) {
            *error = tr("Recovery snapshots are not wired for notebooks yet.");
        }
        return false;
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

    NoteDocument *m_document;
    NotePageView *m_view;
    QTreeWidget *m_navigator;
    NoteSection *m_currentSection = nullptr;
    NotePage *m_currentPage = nullptr;
};

} // namespace QindaOffice::Apps::Notes
