// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_footnotes.h"

#include "docx_properties.h"

static_assert(QindaOffice::Apps::Write::kFootnoteIdProperty == QindaOffice::Docx::kFootnoteIdProperty);
static_assert(QindaOffice::Apps::Write::kEndnoteIdProperty == QindaOffice::Docx::kEndnoteIdProperty);

#include "page_geometry.h"

#include "docx_notes.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

namespace QindaOffice::Apps::Write {

namespace {

// The document-space y of a position, for the page lookup.
qreal documentYAt(const QTextDocument *document, int position)
{
    const QTextBlock block = document->findBlock(position);
    if (!block.isValid()) {
        return 0;
    }
    const QRectF rect = document->documentLayout()->blockBoundingRect(block);
    const QTextLayout *layout = block.layout();
    if (layout == nullptr) {
        return rect.top();
    }
    // Line-accurate, so a marker near a page break lands on the page it is
    // actually printed on rather than on the block's first page.
    const QTextLine line = layout->lineForTextPosition(position - block.position());
    return line.isValid() ? rect.top() + line.y() : rect.top();
}

} // namespace

QList<FootnoteRef> collectFootnotes(const QTextDocument *document, const PageGeometry &geometry)
{
    QList<FootnoteRef> refs;
    if (document == nullptr) {
        return refs;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()
                || !fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
                continue;
            }
            FootnoteRef ref;
            ref.id = fragment.charFormat().property(kFootnoteIdProperty).toString();
            ref.position = fragment.position();
            ref.page = geometry.pageForDocY(documentYAt(document, ref.position)) + 1;
            // Numbering is document order, recomputed every time: inserting
            // a note in the middle renumbers the rest for free.
            ref.number = int(refs.size()) + 1;
            refs.append(ref);
        }
    }
    return refs;
}

QHash<int, QStringList> footnotesByPage(const QTextDocument *document,
                                        const PageGeometry &geometry,
                                        const QHash<QString, QString> &texts)
{
    QHash<int, QStringList> byPage;
    for (const FootnoteRef &ref : collectFootnotes(document, geometry)) {
        const QString text = texts.value(ref.id);
        if (text.isEmpty()) {
            continue;
        }
        byPage[ref.page].append(QStringLiteral("%1  %2").arg(ref.number).arg(text));
    }
    return byPage;
}

namespace {

QString insertMarker(QTextCursor cursor, int property)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    QTextCharFormat marker;
    marker.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    marker.setProperty(property, id);
    // The visible glyph is a placeholder: collectFootnotes owns the real
    // number, and the view re-renders it from document order.
    cursor.insertText(QStringLiteral("*"), marker);
    // Typing after the mark continues plain: PageTextControl's typing format
    // drops a note mark's identity and superscript (page_text_control.cpp).
    return id;
}

} // namespace

QString insertFootnoteMarker(QTextCursor cursor)
{
    return insertMarker(std::move(cursor), kFootnoteIdProperty);
}

QString insertEndnoteMarker(QTextCursor cursor)
{
    return insertMarker(std::move(cursor), kEndnoteIdProperty);
}

QList<EndnoteRef> collectEndnotes(const QTextDocument *document, const Docx::NoteOptions &options)
{
    QList<EndnoteRef> refs;
    if (document == nullptr) {
        return refs;
    }
    const QString format = Docx::noteFormat(options, true);
    int number = options.start;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().hasProperty(kEndnoteIdProperty)) {
                refs.append({fragment.charFormat().stringProperty(kEndnoteIdProperty),
                             Docx::noteNumber(number++, format), fragment.position()});
            }
        }
        if (options.restart == u"eachSect" && block.blockFormat().hasProperty(Docx::kSectionXml)) {
            number = options.start;
        }
    }
    return refs;
}

QString endnoteIdAt(const QTextDocument *document, int position)
{
    if (document == nullptr) {
        return {};
    }
    const QTextBlock block = document->findBlock(position);
    for (QTextBlock::iterator it = block.begin(); block.isValid() && !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.isValid() && fragment.charFormat().hasProperty(kEndnoteIdProperty) && position >= fragment.position()
            && position <= fragment.position() + fragment.length()) {
            return fragment.charFormat().stringProperty(kEndnoteIdProperty);
        }
    }
    return {};
}

QString footnoteIdAt(const QTextDocument *document, int position)
{
    if (document == nullptr) {
        return {};
    }
    const QTextBlock block = document->findBlock(position);
    if (!block.isValid()) {
        return {};
    }
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid() || !fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
            continue;
        }
        // Inclusive of the character just after the marker, so clicking at
        // its right edge (where the caret lands) still finds it.
        if (position >= fragment.position()
            && position <= fragment.position() + fragment.length()) {
            return fragment.charFormat().property(kFootnoteIdProperty).toString();
        }
    }
    return {};
}

void renumberFootnoteMarkers(QTextDocument *document)
{
    renumberNoteMarkers(document, {}, {});
}

void renumberNoteMarkers(QTextDocument *document, const Docx::NoteOptions &footnotes,
                         const Docx::NoteOptions &endnotes)
{
    if (document == nullptr) {
        return;
    }
    struct Marker {
        int position;
        int length;
        int property;
        QString id;
        QString text;
        QString wanted;
    };
    QList<Marker> markers;
    const QString footFormat = Docx::noteFormat(footnotes, false);
    const QString endFormat = Docx::noteFormat(endnotes, true);
    int footNumber = footnotes.start;
    int endNumber = endnotes.start;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat f = fragment.charFormat();
            if (f.hasProperty(kFootnoteIdProperty)) {
                markers.append({fragment.position(), fragment.length(), kFootnoteIdProperty,
                                f.stringProperty(kFootnoteIdProperty), fragment.text(),
                                Docx::noteNumber(footNumber++, footFormat)});
            } else if (f.hasProperty(kEndnoteIdProperty)) {
                markers.append({fragment.position(), fragment.length(), kEndnoteIdProperty,
                                f.stringProperty(kEndnoteIdProperty), fragment.text(),
                                Docx::noteNumber(endNumber++, endFormat)});
            }
        }
        if (block.blockFormat().hasProperty(Docx::kSectionXml)) {
            footNumber = footnotes.restart == u"eachSect" ? footnotes.start : footNumber;
            endNumber = endnotes.restart == u"eachSect" ? endnotes.start : endNumber;
        }
    }
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    // AGENT-GUARD: rewrite from the END backwards. Replacing a marker
    // changes the length of the text before every later position, so a
    // forward pass would edit at stale offsets after the first change.
    for (qsizetype i = markers.size() - 1; i >= 0; --i) {
        const Marker &marker = markers.at(i);
        if (marker.text == marker.wanted) {
            continue;
        }
        cursor.setPosition(marker.position);
        cursor.setPosition(marker.position + marker.length, QTextCursor::KeepAnchor);
        QTextCharFormat format = cursor.charFormat();
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        format.setProperty(marker.property, marker.id);
        cursor.insertText(marker.wanted, format);
    }
    cursor.endEditBlock();
}

int convertNoteMarkers(QTextDocument *document, const QString &mode, QHash<QString, QString> *footToEnd,
                       QHash<QString, QString> *endToFoot)
{
    if (document == nullptr) {
        return 0;
    }
    struct Mark {
        int position;
        int length;
        QTextCharFormat format;
    };
    QList<Mark> marks;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat f = fragment.charFormat();
            const bool foot = f.hasProperty(kFootnoteIdProperty);
            const bool end = f.hasProperty(kEndnoteIdProperty);
            const bool convert = (foot && mode != u"toFootnotes") || (end && mode != u"toEndnotes");
            if (!fragment.isValid() || !convert) {
                continue;
            }
            const int from = foot ? kFootnoteIdProperty : kEndnoteIdProperty;
            const int to = foot ? kEndnoteIdProperty : kFootnoteIdProperty;
            const QString old = f.stringProperty(from);
            QHash<QString, QString> *renamed = foot ? footToEnd : endToFoot;
            if (renamed != nullptr && !renamed->contains(old)) {
                renamed->insert(old, QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
            }
            f.setProperty(to, renamed != nullptr ? renamed->value(old) : old);
            f.clearProperty(from);
            marks.append({fragment.position(), fragment.length(), f});
        }
    }
    QTextCursor cursor(document);
    for (const Mark &mark : std::as_const(marks)) {
        cursor.setPosition(mark.position);
        cursor.setPosition(mark.position + mark.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(mark.format);
    }
    return int(marks.size());
}

void pruneOrphanFootnotes(const QTextDocument *document, QHash<QString, QString> *texts)
{
    if (document == nullptr || texts == nullptr) {
        return;
    }
    QSet<QString> live;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
                live.insert(fragment.charFormat().property(kFootnoteIdProperty).toString());
            }
        }
    }
    // A note whose marker the user deleted would otherwise print forever
    // with no way to reach it.
    for (const QString &id : texts->keys()) {
        if (!live.contains(id)) {
            texts->remove(id);
        }
    }
}

} // namespace QindaOffice::Apps::Write
