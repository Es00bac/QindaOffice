// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_history.h"

#include "note_page_view.h"

namespace QindaOffice::Apps::Notes {

InkCommand::InkCommand(const QString &text, Ink::StrokeLayer before, Ink::StrokeLayer after,
                       NotePageView *view)
    : QUndoCommand(text)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_view(view)
{
}

void InkCommand::undo()
{
    if (m_view != nullptr) {
        m_view->applyHistoryLayer(m_before);
    }
}

void InkCommand::redo()
{
    // The view applied the edit before the push, so the redo() that
    // QUndoStack::push() itself fires must not re-apply it.
    if (m_alreadyApplied) {
        m_alreadyApplied = false;
        return;
    }
    if (m_view != nullptr) {
        m_view->applyHistoryLayer(m_after);
    }
}

} // namespace QindaOffice::Apps::Notes
