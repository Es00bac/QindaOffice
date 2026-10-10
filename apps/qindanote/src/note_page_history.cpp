// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_history.h"

#include "note_page_item.h"

#include <qindatk/rich_text_document.h>

namespace QindaOffice::Apps::Notes {

InkCommand::InkCommand(const QString &text, Ink::StrokeLayer before, Ink::StrokeLayer after,
                       NotePageItem *item)
    : QUndoCommand(text)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_item(item)
{
}

void InkCommand::undo()
{
    if (m_item != nullptr) {
        m_item->applyHistoryLayer(m_before);
    }
}

void InkCommand::redo()
{
    if (m_alreadyApplied) {
        m_alreadyApplied = false;
        return;
    }
    if (m_item != nullptr) {
        m_item->applyHistoryLayer(m_after);
    }
}

BlocksCommand::BlocksCommand(const QString &text, QByteArray before, QByteArray after,
                             NotePageItem *item, int mergeKey)
    : QUndoCommand(text)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_item(item)
    , m_mergeKey(mergeKey)
{
}

void BlocksCommand::undo()
{
    if (m_item != nullptr) {
        m_item->applyHistoryBlocks(m_before);
    }
}

void BlocksCommand::redo()
{
    if (m_alreadyApplied) {
        m_alreadyApplied = false;
        return;
    }
    if (m_item != nullptr) {
        m_item->applyHistoryBlocks(m_after);
    }
}

bool BlocksCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const BlocksCommand *>(other);
    if (next == nullptr || m_mergeKey < 0 || next->m_mergeKey != m_mergeKey) {
        return false;
    }
    m_after = next->m_after;
    return true;
}

BackgroundCommand::BackgroundCommand(QString before, QString after, NotePageItem *item)
    : QUndoCommand(QObject::tr("Background"))
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_item(item)
{
}

void BackgroundCommand::undo()
{
    if (m_item != nullptr) {
        m_item->applyHistoryBackground(m_before);
    }
}

void BackgroundCommand::redo()
{
    if (m_alreadyApplied) {
        m_alreadyApplied = false;
        return;
    }
    if (m_item != nullptr) {
        m_item->applyHistoryBackground(m_after);
    }
}

PageContentCommand::PageContentCommand(QString text, QByteArray before, QByteArray after,
                                      QString oldTitle, QString newTitle, NotePageItem *item)
    : QUndoCommand(std::move(text)), m_before(std::move(before)), m_after(std::move(after)),
      m_oldTitle(std::move(oldTitle)), m_newTitle(std::move(newTitle)), m_item(item)
{
}
void PageContentCommand::undo()
{
    if (m_item) m_item->applyHistoryContent(m_before, m_oldTitle);
}
void PageContentCommand::redo()
{
    if (m_alreadyApplied) { m_alreadyApplied = false; return; }
    if (m_item) m_item->applyHistoryContent(m_after, m_newTitle);
}

RichEditCommand::RichEditCommand(const QString &text, QByteArray before, QByteArray after, NotePageItem *item,
                                 QindaTK::RichTextDocument *document, int block, const QString &transaction,
                                 bool mergeable, int macro)
    : QUndoCommand(text), m_before(std::move(before)), m_after(std::move(after)), m_item(item),
      m_document(document), m_block(block), m_transactions{transaction}, m_mergeable(mergeable), m_macro(macro)
{
}

RichEditCommand::~RichEditCommand()
{
    // The document keeps a receipt per transaction until the host lets go.
    if (m_document != nullptr) {
        for (const QString &transaction : std::as_const(m_transactions)) {
            m_document->releaseHistoryTransaction(transaction);
        }
    }
}

void RichEditCommand::undo()
{
    if (m_item == nullptr) {
        return;
    }
    if (!m_item->replayRichEdit(m_document, m_block, m_transactions, true, m_after, m_before)) {
        m_item->applyHistoryBlocks(m_before);
    }
}

void RichEditCommand::redo()
{
    if (m_alreadyApplied) {
        m_alreadyApplied = false;
        return;
    }
    if (m_item == nullptr) {
        return;
    }
    if (!m_item->replayRichEdit(m_document, m_block, m_transactions, false, m_before, m_after)) {
        m_item->applyHistoryBlocks(m_after);
    }
}

bool RichEditCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const RichEditCommand *>(other);
    if (next == nullptr || next->m_document != m_document || next->m_block != m_block || m_before.isEmpty()
        || next->m_before != m_after) {
        return false;
    }
    const bool sameMacro = m_macro != 0 && next->m_macro == m_macro;
    const bool typing = m_mergeable && next->m_mergeable && next->text() == text();
    if (!sameMacro && !typing) {
        return false;
    }
    m_after = next->m_after;
    m_transactions += next->m_transactions;
    next->m_transactions.clear(); // now ours to release
    return true;
}

} // namespace QindaOffice::Apps::Notes
