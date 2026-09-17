// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The commands built on the three document-scale features QindaWrite gained
// last: footnotes, the table of contents, and tracked changes.
//
// They share a controller because they share a shape — each one reads the
// whole document, writes back through one edit block, and has to leave the
// page numbering consistent afterwards.

#include <QAction>
#include <QObject>

class QMenu;

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;
class WriteRevisions;

class WriteReviewActions final : public QObject {
    Q_OBJECT
public:
    WriteReviewActions(PageTextView *view, WriteDocument *document, WriteRevisions *revisions,
                       QObject *parent);

    void populateInsertMenu(QMenu *menu);
    void populateReviewMenu(QMenu *menu);
    [[nodiscard]] QList<QAction *> stripActions() const;

    // Double-clicking a footnote marker edits its note; the window routes
    // its view's double-click here.
    bool editFootnoteAt(int position);

signals:
    // Something worth saying in the status bar happened.
    void message(const QString &text);

private:
    void createActions();
    void insertFootnote();
    void insertToc();
    void updateToc();
    void syncFromDocument();

    PageTextView *m_view;
    WriteDocument *m_document;
    WriteRevisions *m_revisions;
    QAction *m_insertFootnote = nullptr;
    QAction *m_insertToc = nullptr;
    QAction *m_updateToc = nullptr;
    QAction *m_trackChanges = nullptr;
    QAction *m_acceptChange = nullptr;
    QAction *m_rejectChange = nullptr;
    QAction *m_acceptAll = nullptr;
    QAction *m_rejectAll = nullptr;
    QAction *m_nextChange = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
