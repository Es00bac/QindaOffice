// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote's notebook-structure commands: new page and section, rename,
// delete, reorder, and the navigator's context menu.
//
// Rationale (D-017). The Page menu held exactly two entries — New Page and
// Background… — so a notebook could grow but never be organised: no way to
// add a second section, rename anything, delete a page, or change the order
// of pages. A notebook you cannot tidy stops being useful at about twenty
// pages.
//
// Deletions here are NOT undoable (the notebook has no structural undo
// stack — the page view's history covers ink only), so every delete asks
// first and says exactly what it will remove.

#include <QAction>
#include <QObject>

class QMenu;
class QPoint;

namespace QindaOffice::Apps::Notes {

class NoteWindow;

class NoteStructureActions final : public QObject {
    Q_OBJECT
public:
    explicit NoteStructureActions(NoteWindow *window);

    void populatePageMenu(QMenu *menu);
    void showNavigatorMenu(const QPoint &globalPos);
    [[nodiscard]] QList<QAction *> stripActions() const;
    // Re-reads the current selection so Move Up/Down and the two deletes
    // are only enabled when they can actually do something.
    void syncFromSelection();

private:
    void createActions();
    void newPage();
    void newSection();
    void renameCurrent(bool section);
    void deleteCurrentPage();
    void deleteCurrentSection();
    void movePage(int delta);

    NoteWindow *m_window;
    QAction *m_newPage = nullptr;
    QAction *m_newSection = nullptr;
    QAction *m_renamePage = nullptr;
    QAction *m_renameSection = nullptr;
    QAction *m_deletePage = nullptr;
    QAction *m_deleteSection = nullptr;
    QAction *m_movePageUp = nullptr;
    QAction *m_movePageDown = nullptr;
    QAction *m_background = nullptr;
};

} // namespace QindaOffice::Apps::Notes
