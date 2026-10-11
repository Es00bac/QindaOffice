// SPDX-License-Identifier: GPL-3.0-or-later
// WriteDocument (write_document.h), M77 (D-086): endnotes and the note
// options, the mail merge settings, and documents made from this one
// (merged previews, envelopes, label sheets).

#include "write_document.h"

#include "write_endnotes.h"
#include "write_footnotes.h"
#include "write_pagination.h"
#include "write_sections.h"

#include <QAbstractUndoItem>
#include <QTextCursor>
#include <QTextFrame>

namespace QindaOffice::Apps::Write {

namespace {

// Swaps both note tables with the text's own Undo and Redo.
class NotesUndo final : public QAbstractUndoItem {
public:
    NotesUndo(WriteDocument *document, std::pair<QHash<QString, QString>, QHash<QString, QString>> before,
              std::pair<QHash<QString, QString>, QHash<QString, QString>> after)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void undo() override { m_document->swapNoteTexts(m_before.first, m_before.second); }
    void redo() override { m_document->swapNoteTexts(m_after.first, m_after.second); }

private:
    WriteDocument *m_document;
    std::pair<QHash<QString, QString>, QHash<QString, QString>> m_before;
    std::pair<QHash<QString, QString>, QHash<QString, QString>> m_after;
};

// Note options with the text's Undo and Redo.
class NoteOptionsUndo final : public QAbstractUndoItem {
public:
    NoteOptionsUndo(WriteDocument *document, bool endnote, Docx::NoteOptions before, Docx::NoteOptions after)
        : m_document(document)
        , m_endnote(endnote)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void undo() override { m_document->swapNoteOptions(m_endnote, m_before); }
    void redo() override { m_document->swapNoteOptions(m_endnote, m_after); }

private:
    WriteDocument *m_document;
    bool m_endnote;
    Docx::NoteOptions m_before;
    Docx::NoteOptions m_after;
};

// A page layout with the text's Undo and Redo (envelopes, label sheets).
// AGENT-GUARD: undo() and redo() must not edit the text document: Qt
// itself undoes the root frame's margins (the frame format change the
// layout made inside the same edit block); this restores the rest.
class LayoutUndo final : public QAbstractUndoItem {
public:
    LayoutUndo(WriteDocument *document, Docx::PageLayout before, Docx::PageLayout after)
        : m_document(document)
        , m_before(before)
        , m_after(after)
    {
    }
    void undo() override { m_document->restoreLayout(m_before); }
    void redo() override { m_document->restoreLayout(m_after); }

private:
    WriteDocument *m_document;
    Docx::PageLayout m_before;
    Docx::PageLayout m_after;
};

} // namespace

void WriteDocument::setNoteTexts(QHash<QString, QString> footnotes, QHash<QString, QString> endnotes)
{
    auto before = std::make_pair(m_parts.footnotes, m_parts.endnotes);
    auto after = std::make_pair(footnotes, endnotes);
    swapNoteTexts(std::move(footnotes), std::move(endnotes));
    m_text->appendUndoItem(new NotesUndo(this, std::move(before), std::move(after)));
}

void WriteDocument::swapNoteTexts(QHash<QString, QString> footnotes, QHash<QString, QString> endnotes)
{
    m_parts.footnotes = std::move(footnotes);
    m_parts.endnotes = std::move(endnotes);
    m_endnoteAreaValid = false;
    m_pageFramesValid = false;
    m_text->setModified(true);
    emit contentChanged();
}

void WriteDocument::setNoteOptions(bool endnote, const Docx::NoteOptions &options)
{
    const Docx::NoteOptions before = noteOptions(endnote);
    swapNoteOptions(endnote, options);
    m_text->appendUndoItem(new NoteOptionsUndo(this, endnote, before, options));
    renumberNoteMarkers(m_text.get(), m_parts.footnoteOptions, m_parts.endnoteOptions);
}

void WriteDocument::swapNoteOptions(bool endnote, const Docx::NoteOptions &options)
{
    (endnote ? m_parts.endnoteOptions : m_parts.footnoteOptions) = options;
    m_endnoteAreaValid = false;
    m_pageFramesValid = false;
    m_text->setModified(true);
    emit contentChanged();
}

std::shared_ptr<const EndnoteArea> WriteDocument::endnoteArea() const
{
    if (!m_endnoteAreaValid) {
        m_endnoteArea = m_parts.endnotes.isEmpty()
            ? nullptr
            : layoutEndnotes(*m_text, m_parts.endnotes, m_parts.endnoteOptions,
                             m_sections->padFor(m_sections->count() - 1));
        m_endnoteAreaValid = true;
    }
    return m_endnoteArea;
}

void WriteDocument::setMailMerge(const Docx::MailMergeSettings &settings)
{
    if (settings == m_parts.mailMerge) {
        return;
    }
    m_parts.mailMerge = settings;
    m_text->setModified(true);
    setDirty(true);
}

void WriteDocument::configureLike(QTextDocument *document) const
{
    document->setUseDesignMetrics(true);
    document->setDefaultFont(m_text->defaultFont());
    document->setDefaultTextOption(m_text->defaultTextOption());
    // The closing section's page (M88): the canvas may be wider or taller
    // for another section's sake; a document made from this one is laid out
    // on one page size.
    document->setPageSize(m_parts.layout.pageSize);
    document->setDocumentMargin(m_text->documentMargin());
    QTextFrameFormat root = document->rootFrame()->frameFormat();
    const QTextFrameFormat mine = m_text->rootFrame()->frameFormat();
    root.setLeftMargin(mine.leftMargin());
    root.setRightMargin(mine.rightMargin());
    root.setTopMargin(mine.topMargin());
    root.setBottomMargin(mine.bottomMargin());
    document->rootFrame()->setFrameFormat(root);
}

void WriteDocument::restoreLayout(const Docx::PageLayout &layout)
{
    m_parts.layout = layout;
    m_pageFramesValid = false;
    syncCanvas(); // the page size records no undo item (the frame margins do)
    m_keepRules->schedule(0);
    emit pageSetupChanged();
}

void WriteDocument::setPageLayoutUndoable(const Docx::PageLayout &layout)
{
    const Docx::PageLayout before = m_parts.layout;
    setPageLayout(layout);
    m_text->appendUndoItem(new LayoutUndo(this, before, layout));
}

} // namespace QindaOffice::Apps::Write
