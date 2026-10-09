// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_pagination.h"

#include "docx_format.h"
#include "docx_properties.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <cmath>

namespace QindaOffice::Apps::Write {

namespace {

constexpr int kDefaultDelayMs = 400;
// Word gives up on a keep-with-next run that cannot fit a page; walking a
// longer run back is wasted work.
constexpr int kMaxChain = 12;

// Where a paragraph's lines fall: pages are stacked pageHeight apart in
// document coordinates.
struct Span {
    int firstPage = -1;
    int lastPage = -1;
    int linesOnFirst = 0;
    int lines = 0;
};

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

bool widowControl(const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    return !format.hasProperty(Docx::kWidowControl) || format.boolProperty(Docx::kWidowControl);
}

// One thing the root frame stacks: a paragraph or a table.
struct Item {
    QTextBlock block;
    QTextTable *table = nullptr;

    [[nodiscard]] QTextBlock first() const
    {
        return table != nullptr ? table->cellAt(0, 0).firstCursorPosition().block() : block;
    }
    [[nodiscard]] QTextBlock last() const
    {
        return table != nullptr ? table->lastCursorPosition().block() : block;
    }
    [[nodiscard]] bool keepsWithNext() const { return table == nullptr && Write::keepsWithNext(block); }
    [[nodiscard]] bool hasBreak() const
    {
        return table != nullptr ? table->format().hasProperty(Docx::kSyntheticBreak)
                                : block.blockFormat().hasProperty(Docx::kSyntheticBreak);
    }
};

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
            if (it.currentFrame() == nullptr && keepsWithNext(it.currentBlock())) {
                return true;
            }
        }
    }
    return false;
}

// Groups a pass's format edits into the user's last undo step.
class EditScope {
public:
    explicit EditScope(QTextDocument *document)
        : m_document(document)
        , m_hadUndo(document->isUndoAvailable())
        , m_wasModified(document->isModified())
    {
    }
    ~EditScope()
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
    EditScope(const EditScope &) = delete;
    EditScope &operator=(const EditScope &) = delete;

    void setBreak(const Item &item, bool on)
    {
        QTextCursor cursor(item.first());
        if (m_edited || m_hadUndo) {
            cursor.joinPreviousEditBlock();
        } else {
            cursor.beginEditBlock();
        }
        m_edited = true;
        if (item.table != nullptr) {
            QTextTableFormat format = item.table->format();
            toggle(format, on);
            item.table->setFormat(format);
        } else {
            QTextBlockFormat format = item.block.blockFormat();
            toggle(format, on);
            QTextCursor(item.block).setBlockFormat(format);
        }
        cursor.endEditBlock(); // lays the change out before the next query
    }

private:
    static void toggle(QTextFormat &format, bool on)
    {
        const int policy = format.intProperty(QTextFormat::PageBreakPolicy);
        if (on) {
            format.setProperty(Docx::kSyntheticBreak, policy);
            format.setProperty(QTextFormat::PageBreakPolicy, policy | QTextFormat::PageBreak_AlwaysBefore);
        } else {
            format.setProperty(QTextFormat::PageBreakPolicy, format.intProperty(Docx::kSyntheticBreak));
            format.clearProperty(Docx::kSyntheticBreak);
        }
    }

    QTextDocument *m_document;
    bool m_hadUndo;
    bool m_wasModified;
    bool m_edited = false;
};

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
    QList<Item> items;
    for (auto it = m_document->rootFrame()->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *frame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(frame)) {
                items.append({QTextBlock(), table});
            }
        } else {
            items.append({it.currentBlock(), nullptr});
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

    EditScope edits(m_document);
    for (qsizetype i = start; i < items.size(); ++i) {
        if (items[i].hasBreak()) {
            edits.setBreak(items[i], false);
        }
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
    // `i` moves together with the keep-with-next paragraphs directly before
    // it (a table ends such a run); nothing moves when that run already
    // starts a page — a run longer than a page stays, as in Word.
    auto moveTarget = [&](qsizetype i) -> qsizetype {
        qsizetype head = i;
        while (head > 0 && i - head < kMaxChain && items[head - 1].keepsWithNext()) {
            --head;
        }
        return startsPage(head) ? -1 : head;
    };
    for (qsizetype i = start; i < items.size(); ++i) {
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
            if (item.keepsWithNext() && i + 1 < items.size() && firstPage(i + 1) > span.lastPage) {
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
