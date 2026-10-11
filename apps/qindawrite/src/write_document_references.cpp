// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"
#include <QAbstractUndoItem>
namespace QindaOffice::Apps::Write {
namespace {
class BibliographyUndo final : public QAbstractUndoItem {
public:
    BibliographyUndo(WriteDocument *document, Docx::Bibliography before, Docx::Bibliography after)
        : m_document(document), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_document->swapBibliography(m_before); }
    void redo() override { m_document->swapBibliography(m_after); }
private:
    WriteDocument *m_document;
    Docx::Bibliography m_before, m_after;
};
}
void WriteDocument::setBibliography(const Docx::Bibliography &data)
{
    if (data == m_parts.bibliography) return;
    const auto before = m_parts.bibliography;
    swapBibliography(data);
    m_text->appendUndoItem(new BibliographyUndo(this, before, data));
}
void WriteDocument::swapBibliography(const Docx::Bibliography &data)
{
    m_parts.bibliography = data; m_text->setModified(true); setDirty(true); emit contentChanged();
}
} // namespace QindaOffice::Apps::Write
