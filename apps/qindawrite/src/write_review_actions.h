// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The commands built on the three document-scale features QindaWrite
// gained last: footnotes, the table of contents, and tracked changes.
// They share a controller because they share a shape — each one reads
// the whole document, writes back through one edit block, and has to
// leave the page numbering consistent afterwards. The footnote text
// dialog is a request signal the window answers with commitFootnote().

#include "action_model.h"

#include <QObject>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;
class WriteRevisions;

class WriteReviewActions final : public QObject {
    Q_OBJECT
public:
    WriteReviewActions(PageTextControl *control, WriteDocument *document, WriteRevisions *revisions,
                       Quick::ActionModel *model, QObject *parent);

    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> reviewMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;

    // Double-clicking a footnote marker edits its note; the host routes
    // the item's double-click here. True when a marker was there.
    bool editFootnoteAt(int position);
    // The dialog's answer: an empty `id` inserts a new marker at the
    // caret; an empty `text` on an existing note removes note and marker.
    void commitFootnote(const QString &id, const QString &text);

signals:
    // Something worth saying in the status bar happened.
    void message(const QString &text);
    // Ask for a note's text (`id` empty for a new footnote).
    void footnoteRequested(const QString &id, const QString &text);

private:
    void createActions(Quick::ActionModel *model);
    void insertToc();
    void updateToc();
    void removeMarker(const QString &id);
    void syncFromDocument();

    PageTextControl *m_control;
    WriteDocument *m_document;
    WriteRevisions *m_revisions;
    Quick::ActionEntry *m_insertFootnote = nullptr;
    Quick::ActionEntry *m_insertToc = nullptr;
    Quick::ActionEntry *m_updateToc = nullptr;
    Quick::ActionEntry *m_trackChanges = nullptr;
    Quick::ActionEntry *m_acceptChange = nullptr;
    Quick::ActionEntry *m_rejectChange = nullptr;
    Quick::ActionEntry *m_acceptAll = nullptr;
    Quick::ActionEntry *m_rejectAll = nullptr;
    Quick::ActionEntry *m_nextChange = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
