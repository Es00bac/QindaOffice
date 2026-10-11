// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to the pagination pass (write_pagination.cpp: Word's keep rules;
// write_pagination_sections.cpp: Word sections, M88): what the root frame
// stacks, where its lines fall, and the display-only edits a pass makes.

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextTable>

#include <QList>

#include <functional>

class QAbstractTextDocumentLayout;

namespace QindaOffice::Apps::Write::detail {

// Where a paragraph's lines fall: pages are stacked pageHeight apart in
// document coordinates.
struct Span {
    int firstPage = -1;
    int lastPage = -1;
    int linesOnFirst = 0;
    int lines = 0;
};

[[nodiscard]] Span spanOf(const QTextBlock &block, QAbstractTextDocumentLayout *layout, qreal pageHeight);
[[nodiscard]] bool keepsWithNext(const QTextBlock &block);

// One thing the root frame stacks: a paragraph or a table, and the index
// of the section it lies in (M88).
struct Item {
    QTextBlock block;
    QTextTable *table = nullptr;
    int section = 0;
    bool startsSection = false; // the first item of a section after the first

    [[nodiscard]] QTextBlock first() const
    {
        return table != nullptr ? table->cellAt(0, 0).firstCursorPosition().block() : block;
    }
    [[nodiscard]] QTextBlock last() const
    {
        return table != nullptr ? table->lastCursorPosition().block() : block;
    }
    [[nodiscard]] bool keepsWithNext() const { return table == nullptr && detail::keepsWithNext(block); }
    [[nodiscard]] bool hasBreak() const;
};

// Groups a pass's format edits into the user's last undo step: joined to
// it, or (nothing to undo) dropped from the stack; the modified flag is
// restored either way.
class EditScope {
public:
    explicit EditScope(QTextDocument *document);
    ~EditScope();
    EditScope(const EditScope &) = delete;
    EditScope &operator=(const EditScope &) = delete;

    // A display-only page break before an item, or after a block (`flag`
    // PageBreak_AlwaysAfter: Word's odd / even page start, a table row
    // break), on or off; the block's own flags are kept in kSyntheticBreak.
    void setBreak(const Item &item, bool on);
    void setBlockBreak(const QTextBlock &block, QTextFormat::PageBreakFlags flag, bool on);
    // Any other display-only edit (a section's pad) inside the scope.
    void run(const std::function<void()> &edit);

private:
    QTextDocument *m_document;
    bool m_hadUndo;
    bool m_wasModified;
    bool m_edited = false;
};

// A pass over the sections (M88, write_pagination_sections.cpp): the
// items and which section each canvas page belongs to — the one of the
// first text on it, as SectionLayout::pageSections() decides it.
struct SectionPass {
    QList<Item> &items;
    QAbstractTextDocumentLayout *layout;
    qreal pageHeight;
    QList<int> owner; // canvas page → section; -1 not known yet
    qsizetype assigned = 0; // items whose pages are in `owner`
};

} // namespace QindaOffice::Apps::Write::detail
