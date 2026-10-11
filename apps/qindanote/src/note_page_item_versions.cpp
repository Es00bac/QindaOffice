// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_item.h"

#include <QPointer>
#include <QUndoStack>

namespace QindaOffice::Apps::Notes {
namespace {
class RestoreVersion final : public QUndoCommand {
public:
    RestoreVersion(NotePageItem *item, Ink::PageEnvelope before, Ink::PageEnvelope after)
        : QUndoCommand(NotePageItem::tr("Restore page version")), m_item(item),
          m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { if (m_item) m_item->applyHistoryEnvelope(m_before); }
    void redo() override { if (m_item) m_item->applyHistoryEnvelope(m_after); }
private:
    QPointer<NotePageItem> m_item;
    Ink::PageEnvelope m_before, m_after;
};
}

void NotePageItem::applyHistoryEnvelope(const Ink::PageEnvelope &envelope)
{
    if (!m_page || envelope.id != m_page->id) return;
    // AGENT-GUARD: retain the page's one undo stack. setPage() clears it;
    // restoring through that function would make Restore impossible to undo.
    applyHistoryContent(envelope.blocksJson, envelope.title);
    applyHistoryLayer(envelope.ink);
    applyHistoryBackground(envelope.background);
}

bool NotePageItem::restoreEnvelope(const Ink::PageEnvelope &envelope)
{
    if (!m_page || m_inking || m_erasing || envelope.id != m_page->id) return false;
    commitEdit();
    Ink::PageEnvelope before = m_page->envelope;
    before.title = m_page->title;
    if (Ink::pageToJson(before) == Ink::pageToJson(envelope)) return true;
    m_history->push(new RestoreVersion(this, before, envelope));
    return true;
}
} // namespace QindaOffice::Apps::Notes
