// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_math.h"
// WriteDocument (write_document.h), M88 (D-096): Word sections — their
// settings with Undo, the canvas and pads that lay them out
// (write_sections.h), every page's frame, the headers and footers each
// section keeps or links to.

#include "write_document.h"

#include "docx_properties.h"
#include "write_endnotes.h"
#include "write_pagination.h"
#include "write_sections.h"

#include <QAbstractTextDocumentLayout>
#include <QAbstractUndoItem>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>

#include <cmath>

namespace QindaOffice::Apps::Write {

namespace {

// The closing section's settings (the parts', not a paragraph's) with the
// text's Undo and Redo. AGENT-GUARD: undo() and redo() must not edit the
// text: Qt undoes the canvas margins and pads recorded in the same edit
// block itself; this restores the parts and the canvas page size.
class ClosingSectionUndo final : public QAbstractUndoItem {
public:
    ClosingSectionUndo(WriteDocument *document, Docx::SectionSettings before, Docx::SectionSettings after)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void undo() override { m_document->restoreClosingSection(m_before); }
    void redo() override { m_document->restoreClosingSection(m_after); }

private:
    WriteDocument *m_document;
    Docx::SectionSettings m_before;
    Docx::SectionSettings m_after;
};

// A section's header or footer slot traded with what Link to Previous
// turned it into (the same rule: no text edits in undo() / redo()).
class StoryUndo final : public QAbstractUndoItem {
public:
    StoryUndo(WriteDocument *document, QString key, bool footer, Docx::HeaderFooterSlot slot,
              std::unique_ptr<QTextDocument> other, bool unlinked)
        : m_document(document)
        , m_key(std::move(key))
        , m_footer(footer)
        , m_slot(slot)
        , m_other(std::move(other))
        , m_unlinked(unlinked)
    {
    }
    void undo() override { m_document->swapStory(m_key, m_footer, m_slot, m_other, m_unlinked); }
    void redo() override { m_document->swapStory(m_key, m_footer, m_slot, m_other, m_unlinked); }

private:
    WriteDocument *m_document;
    QString m_key;
    bool m_footer;
    Docx::HeaderFooterSlot m_slot;
    std::unique_ptr<QTextDocument> m_other;
    bool m_unlinked;
};

// Two sections' stories of one slot traded (a split, a deleted break).
class TradeUndo final : public QAbstractUndoItem {
public:
    TradeUndo(WriteDocument *document, QString first, QString second, bool footer, Docx::HeaderFooterSlot slot)
        : m_document(document)
        , m_first(std::move(first))
        , m_second(std::move(second))
        , m_footer(footer)
        , m_slot(slot)
    {
    }
    void undo() override { m_document->tradeStories(m_first, m_second, m_footer, m_slot); }
    void redo() override { m_document->tradeStories(m_first, m_second, m_footer, m_slot); }

private:
    WriteDocument *m_document;
    QString m_first;
    QString m_second;
    bool m_footer;
    Docx::HeaderFooterSlot m_slot;
};

// Different Odd & Even Pages with the text's Undo and Redo.
class OddEvenUndo final : public QAbstractUndoItem {
public:
    OddEvenUndo(WriteDocument *document, bool after)
        : m_document(document)
        , m_after(after)
    {
    }
    void undo() override { m_document->swapDifferentOddEven(!m_after); }
    void redo() override { m_document->swapDifferentOddEven(m_after); }

private:
    WriteDocument *m_document;
    bool m_after;
};

} // namespace

// ---- the sections and the canvas ------------------------------------------------------

const SectionLayout &WriteDocument::sections() const
{
    refreshSections();
    return *m_sections;
}

void WriteDocument::refreshSections() const
{
    if (m_sectionsStale) {
        m_sectionsStale = false;
        m_sections->update();
    }
}

int WriteDocument::sectionAt(int position) const
{
    return sections().sectionAt(position);
}

qreal WriteDocument::contentWidthAt(int position) const
{
    const SectionLayout &layout = sections();
    return qMax<qreal>(48, layout.sections().at(layout.sectionAt(position)).textWidth());
}

void WriteDocument::syncCanvas()
{
    m_syncQueued = false;
    m_sections->update();
    m_sectionsStale = false;
    if (m_text->pageSize() != m_sections->canvasSize()) {
        m_text->setPageSize(m_sections->canvasSize());
        m_pageFramesValid = false;
        m_keepRules->schedule(0);
        emit pageSetupChanged();
    }
}

void WriteDocument::applySections()
{
    applyLayout();
    m_sections->applyPads([](const std::function<void()> &edit) { edit(); });
    m_pageFramesValid = false;
}

void WriteDocument::setSectionSettings(int index, const Docx::SectionSettings &settings)
{
    refreshSections();
    if (index < 0 || index >= m_sections->count()) {
        return;
    }
    emit storiesAboutToBeReplaced(); // its header band may move or change slot
    const Docx::SectionRef ref = m_sections->refs().at(index);
    if (ref.closing()) {
        const Docx::SectionSettings before = Docx::closingSettings(m_parts);
        restoreClosingSection(settings);
        m_text->appendUndoItem(new ClosingSectionUndo(this, before, settings));
    } else {
        QTextBlockFormat format = ref.end.blockFormat();
        format.setProperty(Docx::kSectionSettings, settings.toMap());
        QTextCursor(ref.end).setBlockFormat(format);
    }
    applySections();
    m_text->setModified(true);
}

void WriteDocument::restoreClosingSection(const Docx::SectionSettings &settings)
{
    Docx::setClosingSettings(m_parts, settings);
    syncCanvas();
    m_pageFramesValid = false;
    m_keepRules->schedule(0);
    emit pageSetupChanged();
}

const QList<PageFrame> &WriteDocument::pageFrames() const
{
    if (!m_pageFramesValid) {
        refreshSections();
        const qreal canvas = qMax<qreal>(1, m_text->pageSize().height());
        int pages = qMax(1, int(std::ceil(m_text->documentLayout()->documentSize().height() / canvas)));
        if (const std::shared_ptr<const EndnoteArea> notes = endnoteArea()) {
            pages = qMax(pages, notes->lastPage());
        }
        m_pageFrames = m_sections->pageFrames(pages);
        m_pageFramesValid = true;
    }
    return m_pageFrames;
}

PageFrame WriteDocument::pageFrame(int index) const
{
    PageDecoration decoration;
    decoration.pages = pageFrames();
    return decoration.frame(index, m_text->pageSize());
}

qreal WriteDocument::bodyTop(int page) const
{
    const SectionLayout &layout = sections();
    return layout.sections().at(qBound(0, pageFrame(page).section, layout.count() - 1)).top;
}

qreal WriteDocument::bodyBottom(int page) const
{
    const SectionLayout &layout = sections();
    return layout.sections().at(qBound(0, pageFrame(page).section, layout.count() - 1)).bottom;
}

void WriteDocument::setDifferentOddEven(bool different)
{
    if (m_parts.differentOddEven == different) {
        return;
    }
    swapDifferentOddEven(different);
    m_text->appendUndoItem(new OddEvenUndo(this, different));
}

void WriteDocument::swapDifferentOddEven(bool different)
{
    if (m_parts.differentOddEven == different) {
        return;
    }
    emit storiesAboutToBeReplaced(); // even pages show another slot now
    m_parts.differentOddEven = different;
    m_pageFramesValid = false;
    m_text->setModified(true);
    emit pageSetupChanged();
}

// ---- headers and footers per section ---------------------------------------------------

int WriteDocument::storyKeeper(int section, bool footer, Docx::HeaderFooterSlot slot) const
{
    const SectionLayout &layout = sections();
    const int index = qBound(0, section, layout.count() - 1);
    const int owner = Docx::storyOwner(m_parts, layout.refs(), index, footer, slot);
    if (owner >= 0) {
        return owner;
    }
    // Nothing to link to: the first section that would show it (an empty
    // story a section has but does not define yet counts, so an open
    // editor's typing lands where it opened).
    for (int i = 0; i <= index; ++i) {
        const Docx::HeaderFooter *story = Docx::sectionStory(m_parts, layout.refs().at(i).key, footer, slot);
        if (story != nullptr && story->document) {
            return i;
        }
    }
    return 0;
}

bool WriteDocument::linkedToPrevious(int section, bool footer, Docx::HeaderFooterSlot slot) const
{
    const SectionLayout &layout = sections();
    if (section <= 0 || section >= layout.count()) {
        return false;
    }
    const Docx::HeaderFooter *story = Docx::sectionStory(m_parts, layout.refs().at(section).key, footer, slot);
    return story == nullptr || !story->defines();
}

void WriteDocument::setLinkedToPrevious(int section, bool footer, Docx::HeaderFooterSlot slot, bool linked)
{
    refreshSections();
    if (section <= 0 || section >= m_sections->count() || linkedToPrevious(section, footer, slot) == linked) {
        return;
    }
    const QString key = m_sections->refs().at(section).key;
    std::unique_ptr<QTextDocument> other;
    bool unlinked = false;
    if (!linked) {
        // Word copies what the section showed into a story of its own.
        const QTextDocument *shown = m_sections->story(section, footer, slot);
        other.reset(shown != nullptr ? shown->clone() : nullptr);
        if (other) Docx::installMathLayout(other.get());
        if (!other) {
            other = newStory();
        }
        other->setModified(true); // new content: written from its text
        unlinked = true;
    }
    swapStory(key, footer, slot, other, unlinked);
    m_text->appendUndoItem(new StoryUndo(this, key, footer, slot, std::move(other), unlinked));
    applySections(); // the section's header height may change (Word's push)
    m_text->setModified(true);
}

void WriteDocument::swapStory(const QString &key, bool footer, Docx::HeaderFooterSlot slot,
                              std::unique_ptr<QTextDocument> &document, bool &unlinked)
{
    emit storiesAboutToBeReplaced();
    Docx::HeaderFooter &story = *Docx::sectionStory(m_parts, key, footer, slot, true);
    std::swap(story.document, document);
    std::swap(story.unlinked, unlinked);
    m_sectionsStale = true;
    m_pageFramesValid = false;
    m_text->setModified(true);
    emit pageSetupChanged();
}

void WriteDocument::handOverStories(const QString &from, const QString &to, bool missingOnly)
{
    for (const bool footer : {false, true}) {
        for (int s = 0; s < Docx::kHeaderFooterSlots; ++s) {
            const auto slot = Docx::HeaderFooterSlot(s);
            const Docx::HeaderFooter *mine = Docx::sectionStory(m_parts, from, footer, slot);
            const Docx::HeaderFooter *theirs = Docx::sectionStory(m_parts, to, footer, slot);
            if (mine == nullptr || !mine->defines() || (missingOnly && theirs != nullptr && theirs->defines())) {
                continue;
            }
            tradeStories(from, to, footer, slot);
            m_text->appendUndoItem(new TradeUndo(this, from, to, footer, slot));
        }
    }
}

void WriteDocument::tradeStories(const QString &first, const QString &second, bool footer,
                                 Docx::HeaderFooterSlot slot)
{
    emit storiesAboutToBeReplaced();
    std::swap(*Docx::sectionStory(m_parts, first, footer, slot, true),
              *Docx::sectionStory(m_parts, second, footer, slot, true));
    m_sectionsStale = true;
    m_pageFramesValid = false;
    m_text->setModified(true);
    emit pageSetupChanged();
}

} // namespace QindaOffice::Apps::Write
