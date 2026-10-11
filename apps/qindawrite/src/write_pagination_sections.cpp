// SPDX-License-Identifier: GPL-3.0-or-later
// KeepRules (write_pagination.h), M88 (D-096): Word sections' page breaks.
// A section that starts on a new page starts one, on an odd or even page
// when it says so (a blank page before it, as Word prints one); and where a
// page's section holds less text than the canvas (write_sections.h) the
// page ends where that section's text area does — before the paragraph
// that would run past it, or between two table rows.

#include "write_pagination.h"

#include "docx_properties.h"
#include "write_pagination_p.h"
#include "write_sections.h"

#include <QAbstractTextDocumentLayout>
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
using detail::SectionPass;

namespace {

// The first line of `block` that runs past its page's text area, or -1.
// `limit(page)` is where the text area ends on a canvas page.
int overflowingLine(const QTextBlock &block, QAbstractTextDocumentLayout *layout, qreal pageHeight,
                    const std::function<qreal(int)> &limit)
{
    const QTextLayout *lines = block.isValid() ? block.layout() : nullptr;
    if (lines == nullptr) {
        return -1;
    }
    const qreal origin = layout->blockBoundingRect(block).top();
    for (int i = 0; i < lines->lineCount(); ++i) {
        const QTextLine line = lines->lineAt(i);
        const qreal top = origin + line.y();
        const int page = int(std::floor((top + line.height() / 2) / pageHeight));
        if (top + line.height() > limit(page) + 0.5) {
            return i;
        }
    }
    return -1;
}

// The paragraphs of a table cell (not of a table nested in it).
QList<QTextBlock> cellBlocks(const QTextTableCell &cell)
{
    QList<QTextBlock> out;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        if (it.currentFrame() == nullptr) {
            out.append(it.currentBlock());
        }
    }
    return out;
}

// A cell that starts in `row` and spans no other: the row's own.
QTextTableCell ownCell(const QTextTable *table, int row)
{
    for (int column = 0; column < table->columns(); ++column) {
        const QTextTableCell cell = table->cellAt(row, column);
        if (cell.row() == row && cell.column() == column && cell.rowSpan() == 1) {
            return cell;
        }
    }
    return {};
}

int firstPageOf(const Item &item, QAbstractTextDocumentLayout *layout, qreal pageHeight)
{
    return detail::spanOf(item.first(), layout, pageHeight).firstPage;
}

// Gives the canvas pages item `index`'s lines are on to its section, where
// no earlier text claimed them (SectionLayout::pageSections' rule).
void claimPages(SectionPass &pass, qsizetype index)
{
    const Item &item = pass.items.at(index);
    const int first = firstPageOf(item, pass.layout, pass.pageHeight);
    const int last = detail::spanOf(item.last(), pass.layout, pass.pageHeight).lastPage;
    for (int page = qMax(0, first); first >= 0 && page <= qMax(first, last); ++page) {
        while (pass.owner.size() <= page) {
            pass.owner.append(-1);
        }
        if (pass.owner.at(page) < 0) {
            pass.owner[page] = item.section;
        }
    }
}

bool startsItsPage(const SectionPass &pass, qsizetype index)
{
    if (index == 0) {
        return true;
    }
    const int before = detail::spanOf(pass.items.at(index - 1).last(), pass.layout, pass.pageHeight).lastPage;
    const int self = firstPageOf(pass.items.at(index), pass.layout, pass.pageHeight);
    return before < 0 || self < 0 || before < self;
}

// The item a page break goes before when item `index` must move to the
// next page: with the keep-with-next paragraphs right before it (a heading
// stays with what it heads; never across a section's start), or -1 when
// that run starts its page already (it moves nowhere, as in Word).
qsizetype moveHead(const SectionPass &pass, qsizetype index)
{
    constexpr qsizetype kMaxChain = 12;
    qsizetype head = index;
    while (head > 0 && index - head < kMaxChain && !pass.items.at(head).startsSection
           && pass.items.at(head - 1).keepsWithNext()) {
        --head;
    }
    return startsItsPage(pass, head) ? -1 : head;
}

} // namespace

void KeepRules::clearRowBreaks(const Item &item, EditScope &edits)
{
    if (item.table == nullptr) {
        return;
    }
    for (int row = 0; row < item.table->rows(); ++row) {
        for (int column = 0; column < item.table->columns(); ++column) {
            const QTextTableCell cell = item.table->cellAt(row, column);
            if (cell.row() != row || cell.column() != column) {
                continue;
            }
            for (const QTextBlock &block : cellBlocks(cell)) {
                if (block.blockFormat().hasProperty(Docx::kSyntheticBreak)) {
                    edits.setBlockBreak(block, QTextFormat::PageBreak_Auto, false);
                }
            }
        }
    }
}

qsizetype KeepRules::applySectionRules(SectionPass &pass, qsizetype index, EditScope &edits)
{
    const QList<SectionGeometry> &sections = m_sections->sections();
    const Item &item = pass.items.at(index);
    if (item.section >= sections.size()) {
        return -1;
    }
    // The pages of what came before belong to their sections already.
    while (pass.assigned < index) {
        claimPages(pass, pass.assigned++);
    }
    const SectionGeometry &geometry = sections.at(item.section);
    qsizetype moved = -1; // the first item this moved
    // A section that starts on a new page, on an odd or even one if it says
    // so: Word prints a blank page before it when the next would be wrong.
    if (item.startsSection && index > 0 && geometry.startsPage(item.section)) {
        const QTextBlock end = pass.items.at(index - 1).last();
        const bool before = item.table != nullptr
            ? (item.table->format().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
            : (item.block.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
        if (!before && !(end.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysAfter)) {
            edits.setBreak(item, true);
            m_document->pageCount();
            moved = index;
        }
        const Docx::SectionStart kind = geometry.ref.settings.flow.start;
        if (kind == Docx::SectionStart::EvenPage || kind == Docx::SectionStart::OddPage) {
            const int page = firstPageOf(item, pass.layout, pass.pageHeight) + 1;
            const bool wantEven = kind == Docx::SectionStart::EvenPage;
            if (page > 0 && (page % 2 == 0) != wantEven
                && !(end.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysAfter)) {
                edits.setBlockBreak(end, QTextFormat::PageBreak_AlwaysAfter, true);
                m_document->pageCount();
                moved = index;
            }
        }
    }
    // Its own text height. A page's text area is its section's: the one of
    // the first text on it.
    const qreal canvasTop = m_sections->canvasMargins().top();
    const qreal canvasText = m_sections->canvasTextHeight();
    bool shorter = false;
    for (const SectionGeometry &g : sections) {
        shorter = shorter || g.textHeight() < canvasText - 0.5;
    }
    if (shorter) {
        claimPages(pass, index);
        auto limit = [&](int page) {
            const int owner = page >= 0 && page < pass.owner.size() && pass.owner.at(page) >= 0 ? pass.owner.at(page)
                                                                                                : item.section;
            return page * pass.pageHeight + canvasTop + qMin(canvasText, sections.at(owner).textHeight());
        };
        if (item.table == nullptr) {
            if (overflowingLine(item.block, pass.layout, pass.pageHeight, limit) >= 0) {
                if (const qsizetype head = moveHead(pass, index); head >= 0) {
                    edits.setBreak(pass.items.at(head), true);
                    m_document->pageCount();
                    moved = head;
                }
            }
        } else {
            // Rows past the text area go on to the next page, after the
            // last paragraph of a cell of the row before (Qt repeats the
            // header rows above them).
            const QTextTable *table = item.table;
            for (int guard = table->rows(); guard > 0; --guard) {
                int over = -1;
                for (int row = 0; row < table->rows() && over < 0; ++row) {
                    for (int column = 0; column < table->columns() && over < 0; ++column) {
                        const QTextTableCell cell = table->cellAt(row, column);
                        if (cell.row() != row || cell.column() != column) {
                            continue;
                        }
                        for (const QTextBlock &block : cellBlocks(cell)) {
                            if (overflowingLine(block, pass.layout, pass.pageHeight, limit) >= 0) {
                                over = row;
                                break;
                            }
                        }
                    }
                }
                if (over < 0) {
                    break;
                }
                if (over <= qMax(0, table->format().headerRowCount())) {
                    // Its first rows do not fit: the table starts the next
                    // page, with a heading right before it.
                    if (const qsizetype head = moveHead(pass, index); head >= 0 && !pass.items.at(head).hasBreak()) {
                        edits.setBreak(pass.items.at(head), true);
                        m_document->pageCount();
                        moved = head;
                    }
                    break;
                }
                const QTextTableCell before = ownCell(table, over - 1);
                if (!before.isValid()) {
                    break;
                }
                const QTextBlock last = before.lastCursorPosition().block();
                if (last.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysAfter) {
                    break; // the row starts a page and is taller than it
                }
                edits.setBlockBreak(last, QTextFormat::PageBreak_AlwaysAfter, true);
                m_document->pageCount();
                moved = moved < 0 ? index : moved;
            }
        }
    }
    return moved;
}

} // namespace QindaOffice::Apps::Write
