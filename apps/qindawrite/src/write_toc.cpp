// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_toc.h"

#include "page_geometry.h"
#include "style_catalog.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

// 1..6 for a heading block, 0 for anything else. Title counts as level 1:
// a document's title belongs at the top of its own contents list.
int headingLevelOf(const QTextBlock &block)
{
    if (block.blockFormat().hasProperty(kTocBlockProperty)) {
        return 0; // never index the table of contents itself
    }
    const int style = classifyBlockStyle(block);
    switch (style) {
    case StyleCatalog::Title:
        return 1;
    case StyleCatalog::Heading1:
        return 1;
    case StyleCatalog::Heading2:
        return 2;
    case StyleCatalog::Heading3:
        return 3;
    case StyleCatalog::Heading4:
        return 4;
    case StyleCatalog::Heading5:
        return 5;
    case StyleCatalog::Heading6:
        return 6;
    default:
        return 0;
    }
}

// The block's top in document coordinates, which is what the page geometry
// maps to a page index.
qreal blockTop(const QTextDocument *document, const QTextBlock &block)
{
    const QRectF rect = document->documentLayout()->blockBoundingRect(block);
    return rect.top();
}

// Finds the contiguous run of generated TOC blocks containing `position`,
// or the first run in the document when `position` is negative.
bool findTocRun(const QTextDocument *document, int position, int *from, int *to)
{
    QTextBlock start;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (!block.blockFormat().hasProperty(kTocBlockProperty)) {
            continue;
        }
        const bool inside = position >= block.position()
            && position <= block.position() + block.length() - 1;
        if (position < 0 || inside || !start.isValid()) {
            if (!start.isValid()) {
                start = block;
            }
            if (position < 0) {
                break;
            }
        }
        if (inside) {
            start = block;
            break;
        }
    }
    if (!start.isValid()) {
        return false;
    }
    // Walk back to the head of the run, then forward to its tail.
    QTextBlock head = start;
    while (head.previous().isValid()
           && head.previous().blockFormat().hasProperty(kTocBlockProperty)) {
        head = head.previous();
    }
    QTextBlock tail = head;
    while (tail.next().isValid() && tail.next().blockFormat().hasProperty(kTocBlockProperty)) {
        tail = tail.next();
    }
    *from = head.position();
    *to = tail.position() + tail.length() - 1;
    return true;
}

// Writes the entries at the cursor. The caller owns the edit block.
void writeEntries(QTextCursor &cursor, const QList<TocEntry> &entries)
{
    QTextBlockFormat heading;
    heading.setProperty(kTocBlockProperty, 1);
    QTextCharFormat headingChar;
    headingChar.setFontWeight(QFont::Bold);
    headingChar.setFontPointSize(16);
    cursor.setBlockFormat(heading);
    cursor.setCharFormat(headingChar);
    cursor.insertText(QObject::tr("Contents"));

    for (const TocEntry &entry : entries) {
        QTextBlockFormat block;
        block.setProperty(kTocBlockProperty, 1);
        // Indent by level and put a right-aligned tab at the text width, so
        // the page numbers line up in a column like a printed contents page.
        block.setLeftMargin(16.0 * (entry.level - 1));
        QList<QTextOption::Tab> tabs;
        QTextOption::Tab tab;
        tab.type = QTextOption::RightTab;
        tab.position = 560 - 16.0 * (entry.level - 1);
        tab.delimiter = QLatin1Char('.');
        tabs.append(tab);
        block.setTabPositions(tabs);
        QTextCharFormat plain;
        plain.setFontWeight(entry.level == 1 ? QFont::DemiBold : QFont::Normal);
        cursor.insertBlock(block, plain);
        cursor.insertText(entry.text + QLatin1Char('\t') + QString::number(entry.page));
    }
}

} // namespace

QList<TocEntry> collectHeadings(const QTextDocument *document, const PageGeometry &geometry)
{
    QList<TocEntry> entries;
    if (document == nullptr) {
        return entries;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const int level = headingLevelOf(block);
        const QString text = block.text().trimmed();
        if (level == 0 || text.isEmpty()) {
            continue;
        }
        TocEntry entry;
        entry.text = text;
        entry.level = level;
        entry.position = block.position();
        // The page index comes from the same geometry the view paints with.
        entry.page = geometry.pageForDocY(blockTop(document, block)) + 1;
        entries.append(entry);
    }
    return entries;
}

int insertTableOfContents(QTextDocument *document, const PageGeometry &geometry,
                          const QTextCursor &at)
{
    if (document == nullptr) {
        return 0;
    }
    const QList<TocEntry> entries = collectHeadings(document, geometry);
    QTextCursor cursor = at;
    cursor.beginEditBlock();
    int from = 0;
    int to = 0;
    if (findTocRun(document, cursor.position(), &from, &to)) {
        // The cursor is inside an existing table: replace it rather than
        // nesting a second one inside the first. Removing the run leaves
        // the cursor in an empty block, which is what writeEntries wants.
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else if (!cursor.block().text().isEmpty()) {
        // AGENT-GUARD: writeEntries FORMATS the cursor's block and writes
        // into it, so it must be given an empty one. Writing into a block
        // that already has text turned the first heading into part of the
        // table's own title — and then the table listed one heading fewer.
        if (cursor.atBlockStart()) {
            cursor.insertBlock();
            cursor.movePosition(QTextCursor::PreviousBlock);
        } else {
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
        }
    }
    writeEntries(cursor, entries);
    // Leave the user a normal paragraph after the table, or everything they
    // type next inherits the TOC's block property and joins the table.
    QTextBlockFormat plainBlock;
    cursor.insertBlock(plainBlock, QTextCharFormat());
    cursor.endEditBlock();
    return int(entries.size());
}

int updateTableOfContents(QTextDocument *document, const PageGeometry &geometry)
{
    if (document == nullptr) {
        return -1;
    }
    int from = 0;
    int to = 0;
    if (!findTocRun(document, -1, &from, &to)) {
        return -1;
    }
    const QList<TocEntry> entries = collectHeadings(document, geometry);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    writeEntries(cursor, entries);
    cursor.endEditBlock();
    return int(entries.size());
}

} // namespace QindaOffice::Apps::Write
