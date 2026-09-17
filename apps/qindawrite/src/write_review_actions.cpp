// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_review_actions.h"

#include "page_text_view.h"
#include "write_document.h"
#include "write_footnotes.h"
#include "write_revisions.h"
#include "write_toc.h"

#include "standard_actions.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

WriteReviewActions::WriteReviewActions(PageTextView *view, WriteDocument *document,
                                       WriteRevisions *revisions, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(document)
    , m_revisions(revisions)
{
    createActions();
    connect(m_revisions, &WriteRevisions::changesChanged, this,
            &WriteReviewActions::syncFromDocument);
    connect(m_revisions, &WriteRevisions::recordingChanged, this,
            [this](bool) { syncFromDocument(); });
    syncFromDocument();
}

void WriteReviewActions::createActions()
{
    using namespace Shell::ActionId;
    m_insertFootnote = make(this, kInsertFootnote, tr("&Footnote…"),
                            QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    m_insertToc = make(this, kInsertToc, tr("Table of &Contents"));
    m_updateToc = make(this, kUpdateToc, tr("&Update Table of Contents"), QKeySequence(Qt::Key_F9));
    m_trackChanges = make(this, kReviewTrackChanges, tr("&Track Changes"),
                          QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    m_trackChanges->setCheckable(true);
    m_acceptChange = make(this, kReviewAcceptChange, tr("&Accept Change"));
    m_rejectChange = make(this, kReviewRejectChange, tr("&Reject Change"));
    m_acceptAll = make(this, kReviewAcceptAll, tr("Accept &All Changes"));
    m_rejectAll = make(this, kReviewRejectAll, tr("Reject All C&hanges"));
    m_nextChange = make(this, kReviewNextChange, tr("&Next Change"),
                        QKeySequence(QStringLiteral("Ctrl+Alt+N")));

    connect(m_insertFootnote, &QAction::triggered, this, &WriteReviewActions::insertFootnote);
    connect(m_insertToc, &QAction::triggered, this, &WriteReviewActions::insertToc);
    connect(m_updateToc, &QAction::triggered, this, &WriteReviewActions::updateToc);
    connect(m_trackChanges, &QAction::triggered, this, [this](bool on) {
        if (!m_syncing) {
            m_revisions->setRecording(on);
            emit message(on ? tr("Tracking changes as %1").arg(m_revisions->author())
                            : tr("No longer tracking changes"));
        }
    });
    connect(m_acceptChange, &QAction::triggered, this, [this] {
        if (!m_revisions->acceptAtCursor()) {
            emit message(tr("No tracked change under the cursor"));
        }
    });
    connect(m_rejectChange, &QAction::triggered, this, [this] {
        if (!m_revisions->rejectAtCursor()) {
            emit message(tr("No tracked change under the cursor"));
        }
    });
    connect(m_acceptAll, &QAction::triggered, this, [this] {
        const int count = m_revisions->changeCount();
        m_revisions->acceptAll();
        emit message(tr("Accepted %1 change(s)").arg(count));
    });
    connect(m_rejectAll, &QAction::triggered, this, [this] {
        const int count = m_revisions->changeCount();
        m_revisions->rejectAll();
        emit message(tr("Rejected %1 change(s)").arg(count));
    });
    connect(m_nextChange, &QAction::triggered, this, [this] {
        if (!m_revisions->goToNextChange()) {
            emit message(tr("No tracked changes"));
        }
    });
}

void WriteReviewActions::insertFootnote()
{
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        m_view, tr("Footnote"), tr("Note text:"), QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        return;
    }
    QWidgetTextControl *control = m_view->control();
    QTextCursor cursor = control->textCursor();
    cursor.beginEditBlock();
    const QString id = insertFootnoteMarker(cursor);
    cursor.endEditBlock();
    m_document->setFootnoteText(id, text.trimmed());
    // Numbering is document order, so a note inserted in the middle changes
    // every marker after it — this is what puts that on the page.
    renumberFootnoteMarkers(m_document->textDocument());
    m_view->viewport()->update();
    emit message(tr("Footnote added"));
}

bool WriteReviewActions::editFootnoteAt(int position)
{
    const QString id = footnoteIdAt(m_document->textDocument(), position);
    if (id.isEmpty()) {
        return false;
    }
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        m_view, tr("Footnote"), tr("Note text (empty removes the note):"),
        m_document->footnoteText(id), &ok);
    if (!ok) {
        return true; // handled: the user cancelled
    }
    m_document->setFootnoteText(id, text.trimmed());
    if (text.trimmed().isEmpty()) {
        // The note is gone, so the marker must go with it — a marker
        // pointing at nothing prints a number with no note under it.
        QTextCursor cursor(m_document->textDocument());
        cursor.setPosition(position);
        for (QTextBlock block = m_document->textDocument()->begin(); block.isValid();
             block = block.next()) {
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.isValid()
                    && fragment.charFormat().property(kFootnoteIdProperty).toString() == id) {
                    cursor.setPosition(fragment.position());
                    cursor.setPosition(fragment.position() + fragment.length(),
                                       QTextCursor::KeepAnchor);
                    cursor.removeSelectedText();
                    break;
                }
            }
        }
        renumberFootnoteMarkers(m_document->textDocument());
    }
    m_view->viewport()->update();
    return true;
}

void WriteReviewActions::insertToc()
{
    const int entries = insertTableOfContents(m_document->textDocument(),
                                              *m_view->geometry(),
                                              m_view->control()->textCursor());
    m_document->textDocument()->setModified(true);
    m_view->viewport()->update();
    emit message(entries > 0 ? tr("Table of contents: %1 entries").arg(entries)
                             : tr("No headings to list — apply a heading style first"));
}

void WriteReviewActions::updateToc()
{
    const int entries = updateTableOfContents(m_document->textDocument(), *m_view->geometry());
    if (entries < 0) {
        emit message(tr("This document has no table of contents yet"));
        return;
    }
    m_document->textDocument()->setModified(true);
    m_view->viewport()->update();
    emit message(tr("Table of contents updated: %1 entries").arg(entries));
}

void WriteReviewActions::syncFromDocument()
{
    m_syncing = true;
    m_trackChanges->setChecked(m_revisions->isRecording());
    const bool haveChanges = m_revisions->changeCount() > 0;
    for (QAction *action : {m_acceptChange, m_rejectChange, m_acceptAll, m_rejectAll,
                            m_nextChange}) {
        action->setEnabled(haveChanges);
    }
    m_syncing = false;
}

void WriteReviewActions::populateInsertMenu(QMenu *menu)
{
    menu->addAction(m_insertFootnote);
    menu->addAction(m_insertToc);
    menu->addAction(m_updateToc);
}

void WriteReviewActions::populateReviewMenu(QMenu *menu)
{
    menu->addAction(m_trackChanges);
    menu->addSeparator();
    menu->addAction(m_nextChange);
    menu->addAction(m_acceptChange);
    menu->addAction(m_rejectChange);
    menu->addSeparator();
    menu->addAction(m_acceptAll);
    menu->addAction(m_rejectAll);
}

QList<QAction *> WriteReviewActions::stripActions() const
{
    return {m_trackChanges, m_insertFootnote};
}

} // namespace QindaOffice::Apps::Write
