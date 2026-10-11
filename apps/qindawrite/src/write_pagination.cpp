// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_pagination.h"

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_sections.h"
#include "write_pagination_p.h"
#include "write_sections.h"

#include <QAbstractTextDocumentLayout>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <cmath>

namespace QindaOffice::Apps::Write {

using detail::EditScope;
using detail::Item;
using detail::Span;
using detail::spanOf;

namespace detail {

Span spanOf(const QTextBlock &block, QAbstractTextDocumentLayout *layout, qreal pageHeight)
{
    Span span;
    const QTextLayout *lines = block.isValid() ? block.layout() : nullptr;
    if (lines == nullptr || lines->lineCount() == 0) {
        return span;
    }
    // AGENT-NOTE (Qt 6.11, probed): blockBoundingRect().top() is the block
    // layout's position plus its frames' offsets (a table cell is laid out
    // relative to its table); a line Qt pushes to the next page keeps that
    // position and gets a larger y of its own.
    const qreal origin = layout->blockBoundingRect(block).top();
    for (int i = 0; i < lines->lineCount(); ++i) {
        const QTextLine line = lines->lineAt(i);
        const int page = int(std::floor((origin + line.y() + line.height() / 2) / pageHeight));
        if (span.firstPage < 0) {
            span.firstPage = page;
        }
        span.linesOnFirst += page == span.firstPage ? 1 : 0;
        span.lastPage = page;
        ++span.lines;
    }
    return span;
}

bool keepsWithNext(const QTextBlock &block)
{
    return block.blockFormat().boolProperty(Docx::kKeepWithNext);
}

bool Item::hasBreak() const
{
    return table != nullptr ? table->format().hasProperty(Docx::kSyntheticBreak)
                            : block.blockFormat().hasProperty(Docx::kSyntheticBreak);
}

EditScope::EditScope(QTextDocument *document)
    : m_document(document)
    , m_hadUndo(document->isUndoAvailable())
    , m_wasModified(document->isModified())
{
}

EditScope::~EditScope()
{
    if (m_edited && !m_hadUndo) {
        // Nothing before it to join: the pass's own item would make Undo
        // look available and undo nothing but pagination.
        m_document->clearUndoRedoStacks(QTextDocument::UndoStack);
    }
    if (!m_wasModified && m_document->isModified()) {
        m_document->setModified(false);
    }
}

void EditScope::run(const std::function<void()> &edit)
{
    QTextCursor cursor(m_document);
    if (m_edited || m_hadUndo) {
        cursor.joinPreviousEditBlock();
    } else {
        cursor.beginEditBlock();
    }
    m_edited = true;
    edit();
    cursor.endEditBlock(); // lays the change out before the next query
}

namespace {

// AGENT-GUARD: the block's own flags go into kSyntheticBreak the FIRST
// time a pass adds a flag; a second flag (a break before and after one
// paragraph) must not overwrite them with the first flag's.
void toggle(QTextFormat &format, QTextFormat::PageBreakFlags flag, bool on)
{
    const int policy = format.intProperty(QTextFormat::PageBreakPolicy);
    if (on) {
        if (!format.hasProperty(Docx::kSyntheticBreak)) {
            format.setProperty(Docx::kSyntheticBreak, policy);
        }
        format.setProperty(QTextFormat::PageBreakPolicy, policy | int(flag));
    } else {
        format.setProperty(QTextFormat::PageBreakPolicy, format.intProperty(Docx::kSyntheticBreak));
        format.clearProperty(Docx::kSyntheticBreak);
    }
}

} // namespace

void EditScope::setBreak(const Item &item, bool on)
{
    if (item.table == nullptr) {
        setBlockBreak(item.block, QTextFormat::PageBreak_AlwaysBefore, on);
        return;
    }
    run([&] {
        QTextTableFormat format = item.table->format();
        toggle(format, QTextFormat::PageBreak_AlwaysBefore, on);
        item.table->setFormat(format);
    });
}

void EditScope::setBlockBreak(const QTextBlock &block, QTextFormat::PageBreakFlags flag, bool on)
{
    run([&] {
        QTextBlockFormat format = block.blockFormat();
        toggle(format, flag, on);
        QTextCursor(block).setBlockFormat(format);
    });
}

} // namespace detail

namespace {

constexpr int kDefaultDelayMs = 400;
// Word gives up on a keep-with-next run that cannot fit a page; walking a
// longer run back is wasted work.
constexpr int kMaxChain = 12;

bool widowControl(const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    return !format.hasProperty(Docx::kWidowControl) || format.boolProperty(Docx::kWidowControl);
}

// Word keeps a table row with the next one when its paragraphs say
// keep-with-next; documents use that to keep a table on one page.
bool rowKeepsWithNext(const QTextTable *table, int row)
{
    for (int column = 0; column < table->columns(); ++column) {
        const QTextTableCell cell = table->cellAt(row, column);
        if (cell.row() != row || cell.column() != column) {
            continue; // covered by a merge
        }
        for (auto it = cell.begin(); !it.atEnd(); ++it) {
            if (it.currentFrame() == nullptr && detail::keepsWithNext(it.currentBlock())) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

KeepRules::KeepRules(QTextDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(kDefaultDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &KeepRules::run);
    // Text edits move what follows; a format edit that changes heights
    // shows up as a new document size. Format-only signals alone (the spell
    // checker's highlighting) are not a reason to re-paginate.
    connect(m_document, &QTextDocument::contentsChange, this, [this](int position, int removed, int added) {
        if (!m_applying && removed != added) {
            schedule(position);
        }
    });
    connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] {
        if (!m_applying) {
            schedule(m_from >= 0 ? m_from : 0);
        }
    });
}

void KeepRules::setSections(const SectionLayout *sections, std::function<void()> refresh)
{
    m_sections = sections;
    m_refresh = std::move(refresh);
}

void KeepRules::schedule(int position)
{
    m_from = m_from < 0 ? position : qMin(m_from, position);
    m_timer.start();
}

void KeepRules::applyAll()
{
    m_timer.stop();
    m_from = -1;
    applyFrom(0);
}

int KeepRules::count(const QTextDocument *document)
{
    int found = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        found += block.blockFormat().hasProperty(Docx::kSyntheticBreak) ? 1 : 0;
    }
    for (const QTextFrame *frame : document->rootFrame()->childFrames()) {
        found += frame->frameFormat().hasProperty(Docx::kSyntheticBreak) ? 1 : 0;
    }
    return found;
}

void KeepRules::run()
{
    const int from = m_from;
    m_from = -1;
    if (from >= 0) {
        applyFrom(from);
    }
}

void KeepRules::applyFrom(int position)
{
    const qreal pageHeight = m_document->pageSize().height();
    QAbstractTextDocumentLayout *layout = m_document->documentLayout();
    if (pageHeight <= 0 || layout == nullptr || m_document->isRedoAvailable()) {
        return;
    }
    m_applying = true;
    EditScope edits(m_document);
    // Word sections (M88): the pads that give each section its width
    // first — they move every line after them.
    if (m_sections != nullptr) {
        if (m_refresh) {
            m_refresh();
        }
        if (m_sections->applyPads([&edits](const std::function<void()> &edit) { edits.run(edit); }) > 0) {
            position = 0; // new widths move what came before the edit too
        }
    }
    QList<Item> items;
    const QList<QTextBlock> ends = Docx::sectionEnds(*m_document);
    QSet<int> endNumbers;
    for (const QTextBlock &end : ends) {
        endNumbers.insert(end.blockNumber());
    }
    int section = 0;
    bool fresh = false;
    for (auto it = m_document->rootFrame()->begin(); !it.atEnd(); ++it) {
        Item item;
        item.section = section;
        item.startsSection = fresh;
        if (QTextFrame *frame = it.currentFrame()) {
            auto *table = qobject_cast<QTextTable *>(frame);
            if (table == nullptr) {
                continue;
            }
            item.table = table;
        } else {
            item.block = it.currentBlock();
        }
        fresh = false;
        items.append(item);
        if (item.table == nullptr && endNumbers.contains(item.block.blockNumber())) {
            ++section;
            fresh = true;
        }
    }
    qsizetype start = 0;
    while (start < items.size() && items[start].last().position() + items[start].last().length() <= position) {
        ++start;
    }
    // Re-judge the keep run the change sits in, and what comes before it.
    while (start > 0 && items[start - 1].keepsWithNext()) {
        --start;
    }
    start = qMax<qsizetype>(0, start - 1);

    for (qsizetype i = start; i < items.size(); ++i) {
        if (items[i].hasBreak()) {
            edits.setBreak(items[i], false);
        }
        clearRowBreaks(items[i], edits);
    }
    m_document->pageCount(); // lays the document out

    auto firstPage = [&](qsizetype i) { return spanOf(items[i].first(), layout, pageHeight).firstPage; };
    auto startsPage = [&](qsizetype i) {
        if (i == 0) {
            return true;
        }
        const int before = spanOf(items[i - 1].last(), layout, pageHeight).lastPage;
        const int self = firstPage(i);
        return before < 0 || self < 0 || before < self;
    };
    // A section that starts on a new page (M88) is a wall keep rules do
    // not move things across.
    auto sectionWall = [&](qsizetype i) {
        return m_sections != nullptr && items[i].startsSection && items[i].section < m_sections->count()
            && m_sections->sections().at(items[i].section).startsPage(items[i].section);
    };
    // `i` moves together with the keep-with-next paragraphs directly before
    // it (a table ends such a run); nothing moves when that run already
    // starts a page — a run longer than a page stays, as in Word.
    auto moveTarget = [&](qsizetype i) -> qsizetype {
        qsizetype head = i;
        while (head > 0 && i - head < kMaxChain && !sectionWall(head) && items[head - 1].keepsWithNext()) {
            --head;
        }
        return startsPage(head) ? -1 : head;
    };
    detail::SectionPass sections{items, layout, pageHeight, {}};
    int guard = int(items.size()) * 4 + 64; // every move is forward; a cap all the same
    for (qsizetype i = start; i < items.size() && guard-- > 0; ++i) {
        // Word sections (write_pagination_sections.cpp): a section's start
        // on a new (odd, even) page, and its own text height. A heading that
        // moved with the paragraph it heads: re-judge from there.
        if (m_sections != nullptr) {
            if (const qsizetype moved = applySectionRules(sections, i, edits); moved >= 0 && moved < i) {
                i = moved;
                continue;
            }
        }
        const Item &item = items[i];
        qsizetype target = -1;
        if (item.table != nullptr) {
            // Leading rows that keep with the next stay with it: the table
            // moves when they would be split.
            const int rows = item.table->rows();
            int run = 0;
            while (run < rows - 1 && rowKeepsWithNext(item.table, run)) {
                ++run;
            }
            if (run > 0) {
                const QTextBlock end = item.table->cellAt(run, 0).firstCursorPosition().block();
                if (spanOf(end, layout, pageHeight).firstPage > firstPage(i)) {
                    target = moveTarget(i);
                }
            }
        } else {
            const Span span = spanOf(item.block, layout, pageHeight);
            if (span.lines == 0) {
                continue;
            }
            if (item.keepsWithNext() && i + 1 < items.size() && !sectionWall(i + 1)
                && firstPage(i + 1) > span.lastPage) {
                target = moveTarget(i);
            }
            if (target < 0 && widowControl(item.block) && span.lines > 1 && span.firstPage != span.lastPage
                && (span.linesOnFirst == 1 || span.lines == 2)) {
                target = moveTarget(i);
            }
        }
        if (target >= 0) {
            edits.setBreak(items[target], true);
            m_document->pageCount();
            i = target; // re-judge what follows the moved item
        }
    }
    m_applying = false;
}

} // namespace QindaOffice::Apps::Write
