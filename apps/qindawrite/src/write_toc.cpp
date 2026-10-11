// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_toc.h"

#include "docx_properties.h"
#include "docx_reference_fields.h"
#include "docx_paragraph_format.h"
#include "write_reference_edit.h"
#include <QUuid>

static_assert(QindaOffice::Apps::Write::kTocBlockProperty == QindaOffice::Docx::kWriteTocBlockProperty);

#include "page_geometry.h"
#include "style_catalog.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

namespace QindaOffice::Apps::Write {

namespace {

// 1..9 for a heading block, 0 for anything else. Title counts as level 1:
// a document's title belongs at the top of its own contents list.
int headingLevelOf(const QTextBlock &block)
{
    if (block.blockFormat().stringProperty(Docx::kParagraphStyleId).startsWith(u"TOC") || block.blockFormat().hasProperty(kTocBlockProperty)) {
        return 0; // never index the table of contents itself
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) for (const auto &field : it.fragment().charFormat().property(Docx::kReferenceFields).toList())
        if (Docx::referenceType(field.toMap().value(QStringLiteral("code")).toString()) == u"TOC") return 0;
    if (block.blockFormat().hasProperty(Docx::kOutlineLevel)) {
        const int level = block.blockFormat().intProperty(Docx::kOutlineLevel);
        return level >= 0 && level < 9 ? level + 1 : 0;
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
    for (const auto &span : Docx::referenceSpans(*document)) {
        if (Docx::referenceType(span.field.value(QStringLiteral("code")).toString()) != u"TOC") continue;
        if (position < 0 || (position >= span.start && position <= span.end)) {
            *from = document->findBlock(span.start).position(); *to = document->findBlock(span.end - 1).position() + document->findBlock(span.end - 1).length() - 1;
            if (const auto previous = document->findBlock(*from).previous(); previous.isValid() && (previous.blockFormat().hasProperty(kTocBlockProperty) || previous.blockFormat().stringProperty(Docx::kParagraphStyleId) == u"TOCHeading")) *from = previous.position();
            return true;
        }
    }
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
int writeEntries(QTextCursor &cursor, const QList<TocEntry> &entries, const QVariantMap &options, qreal width)
{
    const int first = qBound(1, options.value(QStringLiteral("first"), 1).toInt(), 9);
    const int last = qBound(first, options.value(QStringLiteral("last"), 6).toInt(), 9);
    const bool links = options.value(QStringLiteral("links"), true).toBool();
    const bool leaders = options.value(QStringLiteral("leaders"), true).toBool();
    const QString code = QStringLiteral("TOC \\o \"%1-%2\"%3 \\z \\u").arg(first).arg(last).arg(links ? QStringLiteral(" \\h") : QString());
    const QVariantMap field = Docx::referenceField(code);
    QTextBlockFormat heading; heading.setProperty(Docx::kParagraphStyleId, QStringLiteral("TOCHeading")); heading.setProperty(kTocBlockProperty, 1); heading.setProperty(Docx::kTocOptions, options);
    QTextCharFormat headingChar; headingChar.setFontWeight(QFont::Bold); headingChar.setFontPointSize(16);
    cursor.setBlockFormat(heading); cursor.setCharFormat(headingChar); cursor.insertText(QObject::tr("Contents"));
    int count = 0;
    for (const TocEntry &entry : entries) {
        if (entry.level < first || entry.level > last) continue;
        const QString bookmark = entry.bookmark;
        QTextBlockFormat block; block.setProperty(Docx::kParagraphStyleId, QStringLiteral("TOC%1").arg(entry.level)); block.setProperty(kTocBlockProperty, 1); block.setProperty(Docx::kTocOptions, options);
        block.setLeftMargin(16.0 * (entry.level - first));
        Docx::setTabStops(block, {{qRound(width * 15), QStringLiteral("right"), leaders ? QStringLiteral("dot") : QStringLiteral("none")}});
        QTextCharFormat plain; plain.setFontWeight(entry.level == first ? QFont::DemiBold : QFont::Normal);
        plain.setProperty(Docx::kReferenceFields, QVariantList{field});
        if (links) { plain.setAnchor(true); plain.setAnchorHref(QLatin1Char('#') + bookmark); }
        cursor.insertBlock(block, plain); cursor.insertText(entry.text + QLatin1Char('\t'), plain);
        auto page = plain;
        page.setProperty(Docx::kReferenceFields, QVariantList{field, Docx::referenceField(QStringLiteral("PAGEREF %1 \\h").arg(bookmark))});
        cursor.insertText(QString::number(entry.page), page); ++count;
    }
    if (count == 0) {
        QTextCharFormat empty; empty.setProperty(Docx::kReferenceFields, QVariantList{field}); empty.setProperty(Docx::kEmptyReference, true);
        cursor.insertBlock(heading, empty); cursor.insertText(QString(QChar(0x200B)), empty);
    }
    return count;
}
QList<TocEntry> bookmarkedHeadings(QTextDocument *document, const PageGeometry &geometry)
{
    auto entries = collectHeadings(document, geometry);
    for (auto &entry : entries) {
        QTextCursor c(document); c.setPosition(entry.position); c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QStringList names = c.charFormat().property(Docx::kBookmarkRanges).toStringList();
        for (const QString &name : names) if (name.startsWith(u"_QindaToc_")) { entry.bookmark = name; break; }
        if (entry.bookmark.isEmpty()) {
            entry.bookmark = QStringLiteral("_QindaToc_") + QUuid::createUuid().toString(QUuid::Id128).left(24);
            names.append(entry.bookmark); QTextCharFormat f; f.setProperty(Docx::kBookmarkRanges, names); c.mergeCharFormat(f);
        }
    }
    return entries;
}
void refreshPages(QTextDocument *document, const PageGeometry &geometry)
{
    // Explicit refresh only, after edits have paginated. Join the transaction
    // so Undo removes both the TOC and the cached page-number corrections.
    document->documentLayout()->documentSize();
    QTextCursor edit(document); edit.joinPreviousEditBlock();
    Docx::Bibliography empty; refreshReferences(*document, empty, geometry.pageHeight(), true);
    edit.endEditBlock();
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
                          const QTextCursor &at, const QVariantMap &options)
{
    if (document == nullptr) {
        return 0;
    }
    QTextCursor cursor = at;
    cursor.beginEditBlock();
    const QList<TocEntry> entries = bookmarkedHeadings(document, geometry);
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
    const int count = writeEntries(cursor, entries, options, geometry.pageSize().width() - document->rootFrame()->frameFormat().leftMargin() - document->rootFrame()->frameFormat().rightMargin());
    // Leave the user a normal paragraph after the table, or everything they
    // type next inherits the TOC's block property and joins the table.
    QTextBlockFormat plainBlock;
    cursor.insertBlock(plainBlock, QTextCharFormat());
    cursor.endEditBlock();
    refreshPages(document, geometry);
    return count;
}

int updateTableOfContents(QTextDocument *document, const PageGeometry &geometry, const QVariantMap &requested)
{
    if (document == nullptr) {
        return -1;
    }
    int from = 0;
    int to = 0;
    if (!findTocRun(document, -1, &from, &to)) {
        return -1;
    }
    for (const auto &span : Docx::referenceSpans(*document)) {
        const auto tokens = Docx::fieldTokens(span.field.value(QStringLiteral("code")).toString());
        if (tokens.value(0).toUpper() != u"TOC") continue;
        if (referenceLocked(span.field)) return -2;
        if (requested.isEmpty()) for (qsizetype i = 1; i < tokens.size(); ++i) {
            if (tokens.at(i) == u"\\h" || tokens.at(i) == u"\\z" || tokens.at(i) == u"\\u") continue;
            if (tokens.at(i) == u"\\o" && i + 1 < tokens.size()) { ++i; continue; }
            return -2;
        }
        break;
    }
    const auto options = requested.isEmpty() ? tableOfContentsOptions(*document) : requested;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    const QList<TocEntry> entries = bookmarkedHeadings(document, geometry);
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    const int count = writeEntries(cursor, entries, options, geometry.pageSize().width() - document->rootFrame()->frameFormat().leftMargin() - document->rootFrame()->frameFormat().rightMargin());
    cursor.endEditBlock();
    refreshPages(document, geometry);
    return count;
}

QVariantMap tableOfContentsOptions(const QTextDocument &document)
{
    for (auto b = document.begin(); b.isValid(); b = b.next()) if (b.blockFormat().hasProperty(Docx::kTocOptions)) return b.blockFormat().property(Docx::kTocOptions).toMap();
    for (const auto &span : Docx::referenceSpans(document)) {
        const auto tokens = Docx::fieldTokens(span.field.value(QStringLiteral("code")).toString());
        if (tokens.value(0).toUpper() != u"TOC") continue;
        const auto range = tokens.value(tokens.indexOf(QStringLiteral("\\o")) + 1).split(QLatin1Char('-'));
        const auto tabs = Docx::tabStops(document.findBlock(span.start).blockFormat());
        return {{QStringLiteral("first"), range.value(0).toInt()}, {QStringLiteral("last"), range.value(1).toInt()},
                {QStringLiteral("links"), tokens.contains(QStringLiteral("\\h"))}, {QStringLiteral("leaders"), !tabs.isEmpty() && tabs.last().leader == u"dot"}};
    }
    return {{QStringLiteral("first"), 1}, {QStringLiteral("last"), 3}, {QStringLiteral("links"), true}, {QStringLiteral("leaders"), true}};
}

} // namespace QindaOffice::Apps::Write
