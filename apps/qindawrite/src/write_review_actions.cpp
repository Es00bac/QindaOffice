// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_review_actions.h"

#include "menu_tree.h"
#include "page_geometry.h"
#include "page_text_control.h"
#include "standard_actions.h"
#include "write_document.h"
#include "write_footnotes.h"
#include "write_revisions.h"
#include "write_toc.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

// The page mapping the TOC prints from: the document's own page size,
// which is what the item paints with (zoom is a view transform).
PageGeometry geometryOf(const WriteDocument *document)
{
    PageGeometry geometry;
    geometry.setPageSize(document->pageSize());
    return geometry;
}

} // namespace

WriteReviewActions::WriteReviewActions(PageTextControl *control, WriteDocument *document,
                                       WriteRevisions *revisions, Quick::ActionModel *model,
                                       QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
    , m_revisions(revisions)
{
    createActions(model);
    connect(m_revisions, &WriteRevisions::changesChanged, this, [this] { syncFromDocument(); });
    connect(m_revisions, &WriteRevisions::recordingChanged, this, [this](bool) { syncFromDocument(); });
    syncFromDocument();
}

void WriteReviewActions::createActions(Quick::ActionModel *model)
{
    using namespace Quick::ActionId;
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    m_insertFootnote = make(kInsertFootnote, tr("&Footnote…"), QKeySequence(QStringLiteral("Ctrl+Alt+F")),
                            "sticky-note");
    m_insertToc = make(kInsertToc, tr("Table of &Contents"));
    m_updateToc = make(kUpdateToc, tr("&Update Table of Contents"), QKeySequence(Qt::Key_F9));
    // Word's own chord (M73): Ctrl+Shift+C is Copy Formatting (write_painter.h).
    m_trackChanges = make(kReviewTrackChanges, tr("&Track Changes"),
                          QKeySequence(QStringLiteral("Ctrl+Shift+E")), "history");
    m_trackChanges->setCheckable(true);
    m_acceptChange = make(kReviewAcceptChange, tr("&Accept Change"));
    m_rejectChange = make(kReviewRejectChange, tr("&Reject Change"));
    m_acceptAll = make(kReviewAcceptAll, tr("Accept &All Changes"));
    m_rejectAll = make(kReviewRejectAll, tr("Reject All C&hanges"));
    m_nextChange = make(kReviewNextChange, tr("&Next Change"), QKeySequence(QStringLiteral("Ctrl+Alt+N")));

    connect(m_insertFootnote, &ActionEntry::triggered, this,
            [this] { emit footnoteRequested(QString(), QString()); });
    connect(m_insertToc, &ActionEntry::triggered, this, [this] { insertToc(); });
    connect(m_updateToc, &ActionEntry::triggered, this, [this] { updateToc(); });
    connect(m_trackChanges, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_revisions->setRecording(on);
            emit message(on ? tr("Tracking changes as %1").arg(m_revisions->author())
                            : tr("No longer tracking changes"));
        }
    });
    connect(m_acceptChange, &ActionEntry::triggered, this, [this] {
        if (!m_revisions->acceptAtCursor()) {
            emit message(tr("No tracked change under the cursor"));
        }
    });
    connect(m_rejectChange, &ActionEntry::triggered, this, [this] {
        if (!m_revisions->rejectAtCursor()) {
            emit message(tr("No tracked change under the cursor"));
        }
    });
    connect(m_acceptAll, &ActionEntry::triggered, this, [this] {
        const int count = m_revisions->changeCount();
        m_revisions->acceptAll();
        emit message(tr("Accepted %1 change(s)").arg(count));
    });
    connect(m_rejectAll, &ActionEntry::triggered, this, [this] {
        const int count = m_revisions->changeCount();
        m_revisions->rejectAll();
        emit message(tr("Rejected %1 change(s)").arg(count));
    });
    connect(m_nextChange, &ActionEntry::triggered, this, [this] {
        if (!m_revisions->goToNextChange()) {
            emit message(tr("No tracked changes"));
        }
    });
}

bool WriteReviewActions::editFootnoteAt(int position)
{
    const QString id = footnoteIdAt(m_document->textDocument(), position);
    if (id.isEmpty()) {
        return false;
    }
    emit footnoteRequested(id, m_document->footnoteText(id));
    return true;
}

void WriteReviewActions::commitFootnote(const QString &id, const QString &rawText)
{
    const QString text = rawText.trimmed();
    if (id.isEmpty()) {
        if (text.isEmpty()) {
            return;
        }
        QTextCursor cursor = m_control->textCursor();
        cursor.beginEditBlock();
        const QString fresh = insertFootnoteMarker(cursor);
        cursor.endEditBlock();
        m_control->setTextCursor(cursor);
        m_document->setFootnoteText(fresh, text);
        // Numbering is document order, so a note inserted in the middle
        // changes every marker after it — this is what puts that on the page.
        renumberFootnoteMarkers(m_document->textDocument());
        emit message(tr("Footnote added"));
        return;
    }
    m_document->setFootnoteText(id, text);
    if (text.isEmpty()) {
        // The note is gone, so the marker must go with it — a marker
        // pointing at nothing prints a number with no note under it.
        removeMarker(id);
        renumberFootnoteMarkers(m_document->textDocument());
    }
}

void WriteReviewActions::removeMarker(const QString &id)
{
    QTextDocument *doc = m_document->textDocument();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()
                || fragment.charFormat().property(kFootnoteIdProperty).toString() != id) {
                continue;
            }
            QTextCursor cursor(doc);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            return;
        }
    }
}

void WriteReviewActions::insertToc()
{
    const int entries = insertTableOfContents(m_document->textDocument(), geometryOf(m_document),
                                              m_control->textCursor());
    m_document->textDocument()->setModified(true);
    emit message(entries > 0 ? tr("Table of contents: %1 entries").arg(entries)
                             : tr("No headings to list — apply a heading style first"));
}

void WriteReviewActions::updateToc()
{
    const int entries = updateTableOfContents(m_document->textDocument(), geometryOf(m_document));
    if (entries < 0) {
        emit message(tr("This document has no table of contents yet"));
        return;
    }
    m_document->textDocument()->setModified(true);
    emit message(tr("Table of contents updated: %1 entries").arg(entries));
}

void WriteReviewActions::syncFromDocument()
{
    m_syncing = true;
    m_trackChanges->setChecked(m_revisions->isRecording());
    const bool haveChanges = m_revisions->changeCount() > 0;
    for (ActionEntry *entry : {m_acceptChange, m_rejectChange, m_acceptAll, m_rejectAll, m_nextChange}) {
        entry->setEnabled(haveChanges);
    }
    m_syncing = false;
}

QList<MenuNode> WriteReviewActions::insertMenuItems() const
{
    return {a(m_insertFootnote), a(m_insertToc), a(m_updateToc)};
}

QList<MenuNode> WriteReviewActions::reviewMenuItems() const
{
    return {a(m_trackChanges), MenuNode::separator(), a(m_nextChange), a(m_acceptChange),
            a(m_rejectChange), MenuNode::separator(), a(m_acceptAll), a(m_rejectAll)};
}

QStringList WriteReviewActions::stripIds() const
{
    return {m_trackChanges->id(), m_insertFootnote->id()};
}

} // namespace QindaOffice::Apps::Write
