// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_history.h"

#include "note_page_item.h"

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

} // namespace QindaOffice::Apps::Notes
